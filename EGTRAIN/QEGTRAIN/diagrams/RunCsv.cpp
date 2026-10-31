#include "diagrams/RunCsv.h"

#include "diagrams/BlockingTimeScope.h"
#include "diagrams/PositionScale.h"
#include "util/CsvWriter.h"
#include "util/TrajectoryUtil.h"

#include <cmath>
#include <cstdint>
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
	const std::vector<std::vector<std::string>>& trajectories,
	const std::vector<BlockSectionExtent>& extents,
	const std::vector<BlockingTimeDiagramSegment>& unmapped) {
	const auto optional = [](double value) {
		return std::isfinite(value) && value >= 0.0 ? csv::formatDouble(value) : std::string();
	};
	const PositionScale scale(extents);
	// The coordinates of an unmapped envelope belong to the route of its own train, so they are left out.
	const auto envelopeRow = [&optional, &scale](const BlockingTimeDiagramSegment& s, bool mapped) {
		const auto coordinate = [mapped](double value) { return mapped ? csv::formatDouble(value) : std::string(); };
		return std::vector<std::string>{s.trainName,
			s.blockId,
			csv::formatDouble(s.startTime),
			csv::formatDouble(s.endTime),
			coordinate(s.midPositionKm),
			blockingSegmentTypeName(s),
			std::string(),
			std::string(),
			std::string(),
			coordinate(s.startPositionKm), coordinate(s.endPositionKm),
			csv::formatDouble(s.originalStartTime), csv::formatDouble(s.originalEndTime),
			optional(s.startApproachTime), optional(s.startRunTime), optional(s.endRunTime),
			optional(s.endClearTime), optional(s.setupTime), optional(s.sightReactionTime),
			optional(s.releaseTime), optional(s.runTimeMargin),
			mapped ? std::string("calculated blocking envelope") : std::string(kUnmappedEnvelopeLayer),
			mapped && s.referenceSection > 0 ? std::to_string(s.referenceSection) : std::string(),
			coordinate(scale.geoAt(s.midPositionKm)),
			coordinate(scale.geoAt(s.startPositionKm, s.endPositionKm)), coordinate(scale.geoAt(s.endPositionKm, s.startPositionKm))};
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
		rows.push_back({reference.trainName,
			std::string(),
			std::string(),
			std::string(),
			csv::formatDouble(reference.positionKm),
			"planned reference",
			reference.eventType,
			reference.stationName,
			csv::formatDouble(reference.time), "", "", "", "", "", "", "", "", "", "", "", "", "planned station event", "",
			csv::formatDouble(scale.geoAt(reference.positionKm))});
	}
	for (const auto& row : trajectories)
		if (!row.empty() && trainInVisibleSet(visibleTrainIds, row.front())) rows.push_back(row);
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
	const std::vector<std::string> header = {
		"Record type", "Leader", "Follower", "Identity", "Operating code", "Original reference[s]",
		"Scheduled reference[s]", "Compressed reference[s]", "Shift[s]", "Scheduled headway[s]",
		"Minimum headway[s]", "Buffer[s]", "Block", "Leader offset[s]", "Follower offset[s]",
		"Candidate headway[s]", "Gap[s]", "Cycle time[s]", "Period[s]", "Cycle / period [%]",
		"Section", "Reference label", "Reference source", "Notes"};
	constexpr std::size_t kColumnCount = 24;
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
	summary[0] = "summary";
	summary[1] = result.firstIdentity;
	summary[2] = result.cycleEndIdentity;
	summary[17] = csv::formatDouble(result.cycleTime);
	summary[18] = csv::formatDouble(result.periodSeconds);
	summary[19] = csv::formatDouble(result.cyclePercentage);
	summary[20] = sectionLabel.toStdString();
	summary[21] = referenceLabel(result.firstIdentity) + " -> " + referenceLabel(result.cycleEndIdentity);
	summary[22] = referenceSource(result.firstIdentity) + " -> " + referenceSource(result.cycleEndIdentity);
	summary[23] = "explicit cycle start and next-period closing occurrence";
	rows.push_back(std::move(summary));
	for (const auto& pair : result.allPairs) {
		if (pair.governingEvidence.empty()) {
			std::vector<std::string> record(kColumnCount);
			record[0] = "pair";
			record[1] = pair.leaderIdentity;
			record[2] = pair.followerIdentity;
			record[9] = csv::formatDouble(pair.scheduledHeadway);
			record[10] = csv::formatDouble(pair.minimumHeadway);
			record[11] = csv::formatDouble(pair.buffer);
			record[20] = sectionLabel.toStdString();
			record[21] = referenceLabel(pair.leaderIdentity) + " -> " + referenceLabel(pair.followerIdentity);
			record[22] = referenceSource(pair.leaderIdentity) + " -> " + referenceSource(pair.followerIdentity);
			record[23] = pair.adjacent ? "no shared constraint" : "nonadjacent; no shared constraint";
			rows.push_back(std::move(record));
			continue;
		}
		for (const auto& evidence : pair.governingEvidence) {
			std::vector<std::string> record(kColumnCount);
			record[0] = "pair";
			record[1] = pair.leaderIdentity;
			record[2] = pair.followerIdentity;
			record[9] = csv::formatDouble(pair.scheduledHeadway);
			record[10] = csv::formatDouble(pair.minimumHeadway);
			record[11] = csv::formatDouble(pair.buffer);
			record[12] = evidence.leaderBlockId + " / " + evidence.followerBlockId;
			record[13] = csv::formatDouble(evidence.leaderOffset);
			record[14] = csv::formatDouble(evidence.followerOffset);
			record[15] = csv::formatDouble(evidence.candidateHeadway);
			record[20] = sectionLabel.toStdString();
			record[21] = referenceLabel(pair.leaderIdentity) + " -> " + referenceLabel(pair.followerIdentity);
			record[22] = referenceSource(pair.leaderIdentity) + " -> " + referenceSource(pair.followerIdentity);
			record[23] = pair.adjacent ? "governing resource" : "nonadjacent ordered constraint; governing resource";
			rows.push_back(std::move(record));
		}
	}
	for (const auto& row : result.compression) {
		const auto appendCompressionRecord = [&](const CapacityCompressionEvidence* governing) {
			const std::string predecessor = governing ? governing->predecessorIdentity : std::string();
			std::vector<std::string> record(kColumnCount);
			record[0] = "compression";
			record[1] = predecessor;
			record[3] = row.identity;
			record[4] = row.operatingCode;
			record[5] = csv::formatDouble(row.originalReference);
			record[6] = csv::formatDouble(row.scheduledReference);
			record[7] = csv::formatDouble(row.compressedReference);
			record[8] = csv::formatDouble(row.shift);
			record[12] = governing ? capacityEvidenceText(governing->governingEvidence) : std::string();
			record[20] = sectionLabel.toStdString();
			record[21] = referenceLabel(row.identity);
			record[22] = referenceSource(row.identity);
			record[23] = "governing predecessor=" + predecessor;
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
		record[0] = "critical";
		record[1] = critical.leaderIdentity;
		record[2] = critical.followerIdentity;
		record[12] = critical.leaderBlockId + " / " + critical.followerBlockId;
		record[16] = csv::formatDouble(critical.gap);
		record[20] = sectionLabel.toStdString();
		record[21] = referenceLabel(critical.leaderIdentity) + " -> " + referenceLabel(critical.followerIdentity);
		record[22] = referenceSource(critical.leaderIdentity) + " -> " + referenceSource(critical.followerIdentity);
		record[23] = "capacity critical block";
		rows.push_back(std::move(record));
	}
	return csv::makeDocument(header, rows);
}

std::string capacityCsvFrom(const CompletedRunData& data, const CapacityAnalysisScope& scope) {
	if (scope.routeIndex < 0 || !capacityScopeIssueFrom(data, scope).empty())
		return {};
	return capacityAnalysisCsv(capacityAnalysisFrom(data, scope), capacityScopeLabelFrom(data, scope));
}
