#include "diagrams/RunExports.h"

#include "diagrams/BlockingTimeScope.h"
#include "diagrams/RouteDiagramCoordinates.h"
#include "diagrams/SimulationHeaders.h"
#include "util/CsvWriter.h"
#include "util/TrajectoryUtil.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>

std::vector<const Train*> runResultTrainPointers() {
	std::vector<const Train*> trains;
	trains.reserve(static_cast<std::size_t>(std::max(0, numRegions)));
	for (int i = 0; i < numRegions; ++i)
		trains.push_back(&regional_train[i]);
	return trains;
}

namespace {

// A train exports when it is in the visible set. An empty set means the user has
// hidden every train, so nothing is exported.
bool trainInVisibleSet(const QStringList& visibleTrainIds, const std::string& trainId) {
	return visibleTrainIds.contains(QString::fromStdString(trainId));
}

} // namespace

std::string csvValue(const RunResultValue& value) {
	return value.available ? csv::formatDouble(value.value) : std::string(csv::kMissingValue);
}

// Per-train trajectory: identity, time, position, speed, power, wheel effort,
// cumulative energy, block. Reads raw simulation samples over the valid
// trajectory ranges so gaps never produce a false row.
std::string buildTrajectoryCsv(const QStringList& visibleTrainIds) {
	std::vector<std::vector<std::string>> rows;
	for (int tr = 0; tr < numRegions; ++tr) {
		const Train& train = regional_train[tr];
		if (!trainInVisibleSet(visibleTrainIds, train.trainDescription))
			continue;
		if (train.earliestActiveTrajectoryIndex < 0)
			continue;
		const auto sampleAt = [](const std::vector<double>& series, int i) {
			return i >= 0 && i < static_cast<int>(series.size())
				? csv::formatDouble(series[static_cast<std::size_t>(i)])
				: std::string(csv::kMissingValue);
		};
		for (const auto& segment : validTrajectorySegments(train.instant_spatial_position,
				 train.earliestActiveTrajectoryIndex,
				 train.End_Time)) {
			for (int i = segment.first; i <= segment.last; ++i) {
				std::string block = i >= 0 && i < static_cast<int>(train.instant_block_section_occupied.size())
					? train.instant_block_section_occupied[static_cast<std::size_t>(i)]
					: std::string(csv::kMissingValue);
				std::vector<std::string> row;
				row.push_back(train.trainDescription);
				row.push_back(train.operatingCode);
				row.push_back(train.serviceId);
				row.push_back(std::to_string(train.serviceOccurrence));
				row.push_back(csv::formatDouble(i * timestep));
				row.push_back(sampleAt(train.instant_spatial_position, i));
				row.push_back(sampleAt(train.instant_train_speed, i));
				std::string power = i >= 0 && i < static_cast<int>(train.instant_train_power_consumption.size())
					? csv::formatDouble(train.instant_train_power_consumption[static_cast<std::size_t>(i)] / 1000.0)
					: std::string(csv::kMissingValue);
				std::string energy = i >= 0 && i < static_cast<int>(train.instant_train_energy_consumption.size())
					? csv::formatDouble(train.instant_train_energy_consumption[static_cast<std::size_t>(i)] * kEnergyMJToKWh)
					: std::string(csv::kMissingValue);
				std::string effort = i >= 0 && i < static_cast<int>(train.instant_train_tractive_effort.size())
					? csv::formatDouble(train.instant_train_tractive_effort[static_cast<std::size_t>(i)] / 1000.0)
					: std::string(csv::kMissingValue);
				row.push_back(power);
				row.push_back(effort);
				row.push_back(energy);
				row.push_back(block);
				rows.push_back(std::move(row));
			}
		}
	}
	if (rows.empty())
		return std::string();
	return csv::makeDocument(
		{"Train", "Operating code", "Service ID", "Occurrence", "Time[s]", "Position[m]", "Speed[m/s]", "Power[kW]", "Tractive effort[kN]", "Energy[kWh]", "Block"}, rows);
}

// Timetable: train, station, planned and simulated arrival and departure, delay.
std::string buildTimetableCsv(const QStringList& visibleTrainIds) {
	const auto results = buildTimetableResults(runResultTrainPointers());
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
	// The coordinates of an unmapped envelope belong to the route of its own train, so they are left out.
	const auto envelopeRow = [&optional](const BlockingTimeDiagramSegment& s, bool mapped) {
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
			mapped && s.referenceSection > 0 ? std::to_string(s.referenceSection) : std::string()};
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
			csv::formatDouble(reference.time), "", "", "", "", "", "", "", "", "", "", "", "", "planned station event"});
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

std::string buildBlockingTimeCsv(const QStringList& visibleTrainIds) {
	const BlockingTimeScope scope = defaultBlockingTimeScope();
	if (train_route.empty()) return {};
	int referenceIndex = -1;
	for (int i = 0; i < numRegions; ++i)
		if (regional_train[i].indexOfRoute >= 0
			&& regional_train[i].indexOfRoute < static_cast<int>(train_route.size())) {
			referenceIndex = regional_train[i].indexOfRoute;
			break;
		}
	if (referenceIndex < 0) return {};
	int omitted = 0;
	const RouteDiagramPath reference = routeDiagramPath(train_route[referenceIndex], nullptr);
	std::map<int, RouteDiagramPath> paths;
	std::map<int, RouteDiagramProjection> projections;
	std::map<std::string, RouteDiagramProjection> byTrain;
	for (int i = 0; i < numRegions; ++i) {
		const Train& train = regional_train[i];
		const int index = train.indexOfRoute;
		if (index < 0 || index >= static_cast<int>(train_route.size())) continue;
		if (!paths.count(index)) {
			paths.emplace(index, routeDiagramPath(train_route[index], nullptr));
			projections.emplace(index, buildRouteDiagramProjection(paths.at(index), reference));
		}
		byTrain.emplace(train.trainDescription, projections.at(index));
	}
	std::vector<BlockingTimeDiagramSegment> unmapped;
	auto segments = projectBlockingSegments(filterBlockingTimeDiagramSegments(buildAllBlockingTimeSegments(), scope.trainIds,
												scope.blockIds, scope.startTime, scope.endTime),
		byTrain, omitted, &unmapped);
	const auto extents = buildBlockSectionExtents(blockSectionInputsForRoute(train_route[referenceIndex]));
	matchBlockingSegmentsToSections(segments, extents, reference.id);
	return buildBlockingTimeCsv(visibleTrainIds, segments,
		filterBlockingTimePlannedReferences(buildBlockingTimePlannedReferences(scope, paths, projections, omitted),
			scope.startTime, scope.endTime),
		{}, extents, unmapped);
}

// Ids of every loaded train, used when a whole result table is exported.
QStringList allTrainIds() {
	QStringList ids;
	for (int i = 0; i < numRegions; ++i)
		ids.append(QString::fromStdString(regional_train[i].trainDescription));
	return ids;
}

// Result windows are non-modal and may outlive the run that created them.
// Capture one document per train now so later filtering never reads a newer run.
std::function<std::string(const QStringList&)> snapshotCsv(
	const std::function<std::string(const QStringList&)>& provider) {
	std::vector<std::pair<QString, std::string>> documents;
	for (const QString& trainId : allTrainIds()) {
		const std::string document = provider(QStringList{trainId});
		if (!document.empty())
			documents.push_back({trainId, document});
	}
	return [documents = std::move(documents)](const QStringList& visibleTrainIds) {
		std::string combined;
		for (const auto& entry : documents) {
			if (!visibleTrainIds.contains(entry.first))
				continue;
			if (combined.empty()) {
				combined = entry.second;
				continue;
			}
			const std::size_t firstRow = entry.second.find('\n');
			if (firstRow != std::string::npos)
				combined.append(entry.second, firstRow + 1, std::string::npos);
		}
		return combined;
	};
}

bool writeCsvFileWithProvenance(const QString& path, const std::string& content,
	const RunProvenance& provenance) {
	return writeRunArtifactWithProvenance(path.toStdString(), "csv", content, provenance);
}
