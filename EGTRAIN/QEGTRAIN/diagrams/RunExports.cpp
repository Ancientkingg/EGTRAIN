#include "diagrams/RunExports.h"

#include "diagrams/BlockingTimeScope.h"
#include "diagrams/RouteDiagramCoordinates.h"
#include "diagrams/SimulationHeaders.h"
#include "util/CsvWriter.h"
#include "util/TrajectoryUtil.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
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
	return trajectoryDocument(rows);
}

// Timetable: train, station, planned and simulated arrival and departure, delay.
std::string buildTimetableCsv(const QStringList& visibleTrainIds) {
	return timetableDocument(buildTimetableResults(runResultTrainPointers()), visibleTrainIds);
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
	const BlockingTimeScope scope = defaultBlockingTimeScope(initial_variables.times * timestep);
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

// --- The same exports from a completed run ---

namespace {

// The route of the first train in the list, or of the first train when the list is empty, that runs on a known route.
int firstRouteOfTrains(const CompletedRunData& data, const std::vector<std::string>& trainIds) {
	for (const CompletedTrain& train : data.trains)
		if ((trainIds.empty() || std::find(trainIds.begin(), trainIds.end(), train.trainId) != trainIds.end())
			&& data.hasRoute(train.routeIndex))
			return train.routeIndex;
	return -1;
}

RouteDiagramPath withoutStations(RouteDiagramPath path) {
	for (RouteDiagramNode& node : path.nodes) {
		node.stationId.clear();
		node.stationName.clear();
	}
	return path;
}

// The paths of the routes that trains run on and their projections onto a reference route.
struct RouteProjections {
	std::map<int, RouteDiagramPath> paths;
	std::map<int, RouteDiagramProjection> projections;
	std::map<std::string, RouteDiagramProjection> byTrain;
};

// Without stations the nodes are matched by node id alone.
RouteProjections projectRoutes(const CompletedRunData& data, const RouteDiagramPath& reference, bool withStations) {
	RouteProjections maps;
	for (const CompletedTrain& train : data.trains) {
		const int index = train.routeIndex;
		if (!data.hasRoute(index))
			continue;
		if (!maps.paths.count(index)) {
			const RouteDiagramPath& path = data.routes[static_cast<std::size_t>(index)].path;
			maps.paths.emplace(index, withStations ? path : withoutStations(path));
			maps.projections.emplace(index, buildRouteDiagramProjection(maps.paths.at(index), reference));
		}
		maps.byTrain.emplace(train.trainId, maps.projections.at(index));
	}
	return maps;
}

// Where a stop lies on the reference route: its node is on the path of the train's route at the stored abscissa.
std::optional<double> stopPosition(const CompletedStop& stop, const RouteDiagramPath& source,
	const RouteDiagramProjection& projection) {
	if (stop.nodeId.empty())
		return {};
	for (const auto& node : source.nodes)
		if (node.nodeId == stop.nodeId && std::abs(node.positionKm - stop.routeX) < 1e-6)
			return projection.map(node.positionKm);
	return {};
}

std::optional<double> stopPosition(const CompletedTrain& train, int index, const RouteDiagramPath& source,
	const RouteDiagramProjection& projection) {
	if (index < 0 || index >= static_cast<int>(train.stops.size()))
		return {};
	return stopPosition(train.stops[static_cast<std::size_t>(index)], source, projection);
}

} // namespace

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

std::vector<BlockingTimeDiagramSegment> blockTimesFrom(const CompletedRunData& data) {
	std::vector<std::vector<BlockingTimeDiagramInput>> trains;
	std::vector<std::string> trainNames;
	trains.reserve(data.trains.size());
	trainNames.reserve(data.trains.size());
	for (const CompletedTrain& train : data.trains) {
		trains.push_back(train.blockTimes);
		trainNames.push_back(train.trainId);
	}
	return buildBlockingTimeDiagramSegments(trains, trainNames);
}

BlockingTimeScope defaultBlockingTimeScopeFrom(const CompletedRunData& data) {
	return defaultBlockingTimeScope(data.settings.horizonSeconds());
}

BlockingTimeScopeChoices blockingTimeScopeChoicesFrom(const CompletedRunData& data) {
	BlockingTimeScopeChoices choices;
	choices.caseEndSeconds = data.settings.horizonSeconds();
	choices.stepSeconds = data.settings.timestepSeconds;
	for (const CompletedRoute& route : data.routes) {
		BlockingTimeScopeChoices::Route choice;
		choice.id = route.id;
		choice.corridor = route.corridor;
		for (const BlockSectionInput& block : route.blocks)
			choice.blockIds.push_back(block.id);
		choices.routes.push_back(std::move(choice));
	}
	for (const CompletedTrain& train : data.trains)
		choices.trains.push_back({train.trainId, train.routeIndex, train.routeId});
	return choices;
}

std::vector<BlockingTimePlannedReference> plannedReferencesFrom(const CompletedRunData& data,
	const BlockingTimeScope& scope, const std::map<int, RouteDiagramPath>& paths,
	const std::map<int, RouteDiagramProjection>& projections, int& omitted) {
	std::vector<BlockingTimePlannedReference> references;
	const auto selectedSections = data.hasRoute(scope.routeIndex) && !scope.sectionIndices.empty()
		? scopedBlockSectionExtents(buildBlockSectionExtents(data.routes[static_cast<std::size_t>(scope.routeIndex)].blocks), scope.sectionIndices)
		: std::vector<BlockSectionExtent>();
	if (scope.routeIndex >= 0 && scope.trainIds.empty())
		return references;
	for (const CompletedTrain& train : data.trains) {
		if (!scope.trainIds.empty() && std::find(scope.trainIds.begin(), scope.trainIds.end(), train.trainId) == scope.trainIds.end())
			continue;
		for (const CompletedStop& stop : train.stops) {
			if (!stop.onRoute(scope.blockIds))
				continue;
			const auto path = paths.find(train.routeIndex);
			const auto projection = projections.find(train.routeIndex);
			const auto positionKm = path == paths.end() || projection == projections.end()
				? std::optional<double>()
				: stopPosition(stop, path->second, projection->second);
			if (!positionKm) {
				++omitted;
				references.push_back({train.trainId, "", "", std::numeric_limits<double>::quiet_NaN(), 0.0});
				continue;
			}
			if (!scope.sectionIndices.empty() && !positionInBlockSections(*positionKm, selectedSections)) {
				references.push_back({train.trainId, "", "", std::numeric_limits<double>::quiet_NaN(), 0.0});
				continue; // Preserve the gap; do not bridge another visit of a repeated resource.
			}
			const auto append = [&](const char* eventType, double seconds) {
				if (!std::isfinite(seconds) || seconds < 0.0)
					return;
				references.push_back({train.trainId, stop.station, eventType, seconds, *positionKm});
			};
			append("arrival", stop.scheduledArrival);
			append("departure", stop.scheduledDeparture);
		}
	}
	return references;
}

std::string blockingTimeCsvFrom(const CompletedRunData& data, const QStringList& visibleTrainIds) {
	const BlockingTimeScope scope = defaultBlockingTimeScopeFrom(data);
	const int referenceIndex = firstRouteOfTrains(data, {});
	if (referenceIndex < 0)
		return {};
	int omitted = 0;
	const RouteDiagramPath reference = withoutStations(data.routes[static_cast<std::size_t>(referenceIndex)].path);
	const RouteProjections maps = projectRoutes(data, reference, false);
	std::vector<BlockingTimeDiagramSegment> unmapped;
	auto segments = projectBlockingSegments(filterBlockingTimeDiagramSegments(blockTimesFrom(data), scope.trainIds,
												scope.blockIds, scope.startTime, scope.endTime),
		maps.byTrain, omitted, &unmapped);
	const auto extents = buildBlockSectionExtents(data.routes[static_cast<std::size_t>(referenceIndex)].blocks);
	matchBlockingSegmentsToSections(segments, extents, reference.id);
	return buildBlockingTimeCsv(visibleTrainIds, segments,
		filterBlockingTimePlannedReferences(plannedReferencesFrom(data, scope, maps.paths, maps.projections, omitted),
			scope.startTime, scope.endTime),
		{}, extents, unmapped);
}

std::optional<BlockingTimeDiagramData> blockingTimeDiagramData(const CompletedRunData& data,
	const BlockingTimeScope& scope) {
	const std::vector<BlockingTimeDiagramSegment> scoped = scope.routeIndex >= 0 && scope.trainIds.empty()
		? std::vector<BlockingTimeDiagramSegment>()
		: filterBlockingTimeDiagramSegments(blockTimesFrom(data), scope.trainIds, scope.blockIds,
			  scope.startTime, scope.endTime);
	const int referenceIndex = scope.routeIndex >= 0 ? scope.routeIndex : firstRouteOfTrains(data, scope.trainIds);
	if (!data.hasRoute(referenceIndex))
		return std::nullopt;
	const CompletedRoute& referenceRoute = data.routes[static_cast<std::size_t>(referenceIndex)];

	BlockingTimeDiagramData result;
	result.referencePath = referenceRoute.path;
	const RouteProjections maps = projectRoutes(data, result.referencePath, true);
	result.segments = projectBlockingSegments(scoped, maps.byTrain, result.omitted, &result.unmapped);
	const auto allExtents = buildBlockSectionExtents(referenceRoute.blocks);
	result.extents = scopedBlockSectionExtents(allExtents, scope.sectionIndices);
	matchBlockingSegmentsToSections(result.segments, allExtents, result.referencePath.id);
	if (!scope.sectionIndices.empty())
		result.segments.erase(std::remove_if(result.segments.begin(), result.segments.end(),
								  [&result](const auto& segment) { return !blockingSegmentInSections(segment, result.extents); }),
			result.segments.end());
	const auto allPlanned = plannedReferencesFrom(data, scope, maps.paths, maps.projections, result.omitted);
	result.plannedReferences = filterBlockingTimePlannedReferences(allPlanned, scope.startTime, scope.endTime);
	result.plannedLines = clipBlockingTimePlannedReferences(allPlanned, scope.startTime, scope.endTime);

	const double timestepSeconds = data.settings.timestepSeconds;
	for (const CompletedTrain& train : data.trains) {
		const CompletedTrajectory& trajectory = train.trajectory;
		const auto projection = maps.byTrain.find(train.trainId);
		if (trajectory.segments.empty() || projection == maps.byTrain.end())
			continue;
		std::vector<const BlockingTimeDiagramSegment*> own;
		for (const auto& block : result.segments)
			if (block.trainName == train.trainId)
				own.push_back(&block);
		for (const TrajectorySegment& valid : trajectory.segments) {
			bool open = false;
			for (int t = valid.first; t <= valid.last; ++t) {
				const double seconds = t * timestepSeconds;
				const double sourceKm = routeDiagramTrajectoryKm(trajectory.at(trajectory.position, t).value_or(0.0));
				const auto x = projection->second.map(sourceKm);
				const bool inScope = x && seconds >= scope.startTime && seconds <= scope.endTime
					&& std::any_of(own.begin(), own.end(), [&](const BlockingTimeDiagramSegment* block) {
						   return seconds >= block->startTime && seconds <= block->endTime
							   && *x >= std::min(block->startPositionKm, block->endPositionKm)
							   && *x <= std::max(block->startPositionKm, block->endPositionKm);
					   });
				if (!inScope) {
					open = false;
					continue;
				}
				if (!open) {
					result.trajectories.push_back({train.trainId, {}});
					open = true;
				}
				result.trajectories.back().points.emplace_back(*x, seconds);
				result.trajectoryRows.push_back({train.trainId, "", "", "", csv::formatDouble(*x),
					"recorded sample", "", "", csv::formatDouble(seconds), "", "", "", "", "", "", "", "", "", "", "", "", "recorded trajectory"});
			}
		}
	}
	return result;
}

std::string capacityEvidenceText(const std::vector<CapacityHeadwayEvidence>& evidence) {
	QStringList values;
	for (const auto& item : evidence)
		values << QString("%1/%2 (leader=%3 s; follower=%4 s; candidate=%5 s)")
					  .arg(QString::fromStdString(item.leaderBlockId), QString::fromStdString(item.followerBlockId),
						  QString::fromStdString(csv::formatDouble(item.leaderOffset)),
						  QString::fromStdString(csv::formatDouble(item.followerOffset)),
						  QString::fromStdString(csv::formatDouble(item.candidateHeadway)));
	return values.join("; ").toStdString();
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

std::map<std::string, RouteDiagramProjection> routeProjectionsByTrain(const CompletedRunData& data,
	const RouteDiagramPath& reference) {
	return projectRoutes(data, reference, true).byTrain;
}

std::string capacityCsvFrom(const CompletedRunData& data, const CapacityAnalysisScope& scope) {
	if (scope.routeIndex < 0 || !capacityScopeIssueFrom(data, scope).empty())
		return {};
	return capacityAnalysisCsv(analyzeCapacity(capacityInputsFrom(data, scope), scope.periodSeconds, scope.cycleEndOccurrenceId),
		capacityScopeLabelFrom(data, scope));
}

std::vector<std::pair<int, std::string>> usedRouteReferencesFrom(const CompletedRunData& data) {
	std::vector<std::pair<int, std::string>> used;
	for (const CompletedTrain& train : data.trains) {
		const int index = train.routeIndex;
		if (data.hasRoute(index)
			&& std::none_of(used.begin(), used.end(), [index](const auto& route) { return route.first == index; }))
			used.emplace_back(index, data.routes[static_cast<std::size_t>(index)].id);
	}
	return used;
}

std::optional<RouteDiagramData> routeDiagramData(const CompletedRunData& data, bool timetable, int referenceIndex) {
	if (!data.hasRoute(referenceIndex))
		return std::nullopt;
	RouteDiagramData result;
	result.referencePath = data.routes[static_cast<std::size_t>(referenceIndex)].path;
	if (result.referencePath.nodes.empty())
		return std::nullopt;
	const RouteProjections maps = projectRoutes(data, result.referencePath, true);
	const auto record = [&](const CompletedTrain& train, const char* kind, const std::string& station,
							int journey, int call, const RunResultValue& time, std::optional<double> x) {
		result.csvRows.push_back({train.trainId, result.referencePath.id, data.routes[static_cast<std::size_t>(train.routeIndex)].id,
			kind, station, journey ? std::to_string(journey) : "", call ? std::to_string(call) : "",
			time.available ? csv::formatDouble(time.value) : "", x ? csv::formatDouble(*x) : ""});
	};
	for (std::size_t trainIndex = 0; trainIndex < data.trains.size(); ++trainIndex) {
		const CompletedTrain& train = data.trains[trainIndex];
		const auto path = maps.paths.find(train.routeIndex);
		const auto projection = maps.projections.find(train.routeIndex);
		if (path == maps.paths.end() || projection == maps.projections.end())
			continue;
		const auto newSeries = [&](RouteDiagramSeries::Kind kind, const std::string& event, bool planned) {
			RouteDiagramSeries series;
			series.kind = kind;
			series.trainId = train.trainId;
			series.trainIndex = static_cast<int>(trainIndex);
			series.event = event;
			series.planned = planned;
			return series;
		};
		if (timetable) {
			std::vector<const TimetableResultRow*> rows;
			for (const TimetableResultRow& row : data.timetable)
				if (row.trainId == train.trainId)
					rows.push_back(&row);
			// Separate series at every absent or unprojectable event. Never connect
			// across a missing schedule value or an unavailable station visit.
			for (int kind = 0; kind < 4; ++kind) {
				const char* event = kind == 0 ? "planned arrival" : kind == 1 ? "planned departure"
					: kind == 2												  ? "simulated arrival"
																			  : "simulated departure";
				bool open = false;
				for (const TimetableResultRow* row : rows) {
					const auto x = stopPosition(train, row->journeyIndex - 1, path->second, projection->second);
					const RunResultValue time = kind == 0 ? row->plannedArrivalSeconds : kind == 1 ? row->plannedDepartureSeconds
						: kind == 2																   ? row->simulatedArrivalSeconds
																								   : row->simulatedDepartureSeconds;
					record(train, event, row->stationId, row->journeyIndex, row->callIndex, time, x);
					if (!x || !time.available) {
						open = false;
						continue;
					}
					if (!open) {
						result.series.push_back(newSeries(RouteDiagramSeries::Kind::Event, event, kind < 2));
						open = true;
					}
					result.series.back().points.push_back({*x, time.value, row->stationId, row->journeyIndex, row->callIndex});
				}
			}
			// A dwell is vertical: fixed reference distance, advancing time.
			for (const TimetableResultRow* row : rows) {
				const auto x = stopPosition(train, row->journeyIndex - 1, path->second, projection->second);
				for (int planned = 0; planned < 2; ++planned) {
					const auto arrival = planned ? row->plannedArrivalSeconds : row->simulatedArrivalSeconds;
					const auto departure = planned ? row->plannedDepartureSeconds : row->simulatedDepartureSeconds;
					if (!x || !arrival.available || !departure.available)
						continue;
					RouteDiagramSeries dwell = newSeries(RouteDiagramSeries::Kind::Dwell, planned ? "planned dwell" : "simulated dwell", planned != 0);
					dwell.points.push_back({*x, arrival.value, row->stationId, row->journeyIndex, row->callIndex});
					dwell.points.push_back({*x, departure.value, row->stationId, row->journeyIndex, row->callIndex});
					result.series.push_back(std::move(dwell));
				}
			}
		} else {
			const CompletedTrajectory& trajectory = train.trajectory;
			for (const TrajectorySegment& valid : trajectory.segments) {
				bool open = false;
				for (int t = valid.first; t <= valid.last; ++t) {
					const double sourceKm = routeDiagramTrajectoryKm(trajectory.at(trajectory.position, t).value_or(0.0));
					const auto x = projection->second.map(sourceKm);
					const RunResultValue time{true, t * data.settings.timestepSeconds};
					record(train, "trajectory", "", 0, 0, time, x);
					if (!x) {
						open = false;
						continue;
					}
					if (!open) {
						RouteDiagramSeries line = newSeries(RouteDiagramSeries::Kind::Trajectory, std::string(), false);
						line.routeId = data.routes[static_cast<std::size_t>(train.routeIndex)].id;
						result.series.push_back(std::move(line));
						open = true;
					}
					result.series.back().points.push_back({*x, time.value, std::string(), 0, 0});
				}
			}
		}
	}
	return result;
}
