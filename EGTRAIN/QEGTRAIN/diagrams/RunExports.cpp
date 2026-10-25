#include "diagrams/RunExports.h"

#include "diagrams/BlockingTimeScope.h"
#include "diagrams/PositionScale.h"
#include "diagrams/RouteDiagramCoordinates.h"
#include "util/CsvWriter.h"
#include "util/TrajectoryUtil.h"

#include <QAbstractItemModel>
#include <QHeaderView>
#include <QStyle>
#include <QTableView>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
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

namespace {

// The route of the first train in the list, or of the first train when the list is empty, that runs on a known route.
int firstRouteOfTrains(const CompletedRunData& data, const std::vector<std::string>& trainIds) {
	for (const CompletedTrain& train : data.trains)
		if ((trainIds.empty() || std::find(trainIds.begin(), trainIds.end(), train.trainId) != trainIds.end())
			&& data.hasRoute(train.routeIndex))
			return train.routeIndex;
	return -1;
}

// The paths of the routes that trains run on and their projections onto a reference route.
struct RouteProjections {
	std::map<int, RouteDiagramPath> paths;
	std::map<int, RouteDiagramProjection> projections;
	std::map<std::string, RouteDiagramProjection> byTrain;
};

// Projects the route of every train onto the reference route.
RouteProjections projectRoutes(const CompletedRunData& data, const RouteDiagramPath& reference) {
	RouteProjections maps;
	for (const CompletedTrain& train : data.trains) {
		const int index = train.routeIndex;
		if (!data.hasRoute(index))
			continue;
		if (!maps.paths.count(index)) {
			maps.paths.emplace(index, data.routes[static_cast<std::size_t>(index)].path);
			maps.projections.emplace(index, buildRouteDiagramProjection(maps.paths.at(index), reference));
		}
		maps.byTrain.emplace(train.trainId, maps.projections.at(index));
	}
	return maps;
}

// Where the stop with the given index lies on the reference route.
std::optional<double> stopPosition(const CompletedTrain& train, int index, const RouteDiagramPath& source,
	const RouteDiagramProjection& projection) {
	if (index < 0 || index >= static_cast<int>(train.stops.size()))
		return {};
	return routeDiagramStopPosition(train.stops[static_cast<std::size_t>(index)], source, projection);
}

// Adds to the diagram the recorded samples of each train that lie inside one of its envelopes there, each moved in time
// by the shift of its train in `shifts` (0 s for a train without one). A repeated train of the diagram has the samples of
// the train it repeats.
void addRecordedTrajectories(BlockingTimeDiagramData& diagram, const CompletedRunData& data,
	const std::map<std::string, RouteDiagramProjection>& projections, const std::map<std::string, double>& shifts) {
	const double timestepSeconds = data.settings.timestepSeconds;
	const PositionScale scale(diagram.extents);
	std::vector<std::pair<const CompletedTrain*, std::string>> sampled; // A train of the run and the id it has in the chart
	for (const CompletedTrain& train : data.trains)
		sampled.emplace_back(&train, train.trainId);
	for (const auto& [repeat, source] : diagram.repeatedTrains)
		for (const CompletedTrain& train : data.trains)
			if (train.trainId == source)
				sampled.emplace_back(&train, repeat);
	for (const auto& [train, trainId] : sampled) {
		const CompletedTrajectory& trajectory = train->trajectory;
		const auto projection = projections.find(trainId);
		if (trajectory.segments.empty() || projection == projections.end())
			continue;
		const auto shift = shifts.find(trainId);
		const double shiftSeconds = shift == shifts.end() ? 0.0 : shift->second;
		std::vector<const BlockingTimeDiagramSegment*> own;
		for (const auto& block : diagram.segments)
			if (block.trainName == trainId)
				own.push_back(&block);
		for (const TrajectorySegment& valid : trajectory.segments) {
			bool open = false;
			for (int t = valid.first; t <= valid.last; ++t) {
				const double seconds = t * timestepSeconds + shiftSeconds;
				const double sourceKm = routeDiagramTrajectoryKm(trajectory.at(trajectory.position, t).value_or(0.0));
				const auto x = projection->second.map(sourceKm);
				const bool inside = x && std::any_of(own.begin(), own.end(), [&](const BlockingTimeDiagramSegment* block) {
					return seconds >= block->startTime && seconds <= block->endTime
						&& *x >= std::min(block->startPositionKm, block->endPositionKm)
						&& *x <= std::max(block->startPositionKm, block->endPositionKm);
				});
				if (!inside) {
					open = false;
					continue;
				}
				if (!open) {
					diagram.trajectories.push_back({trainId, {}});
					open = true;
				}
				diagram.trajectories.back().points.emplace_back(*x, seconds);
				diagram.trajectoryRows.push_back({trainId, "", "", "", csv::formatDouble(*x),
					"recorded sample", "", "", csv::formatDouble(seconds), "", "", "", "", "", "", "", "", "", "", "", "", "recorded trajectory",
					"", csv::formatDouble(scale.geoAt(*x))});
			}
		}
	}
}

// Places a segment that the route of its train does not project onto the reference route on the sections of `extents`
// that share a resource with its block, from the start of the first of them to the end of the last. False when no
// section with geometry shares one.
bool placeOnSharedSections(BlockingTimeDiagramSegment& segment, const std::vector<BlockSectionExtent>& extents) {
	const BlockSectionExtent* first = nullptr;
	const BlockSectionExtent* last = nullptr;
	for (const BlockSectionExtent& extent : extents) {
		if (!extent.geometryValid || !shareBlockingTimeResource(extent.id, segment.blockId))
			continue;
		if (!first)
			first = &extent;
		last = &extent;
	}
	if (!first)
		return false;
	segment.startPositionKm = first->startKm;
	segment.endPositionKm = last->endKm;
	segment.midPositionKm = (segment.startPositionKm + segment.endPositionKm) / 2.0;
	return true;
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
	choices.sectionLabels = data.sectionLabels;
	for (const CompletedRoute& route : data.routes) {
		BlockingTimeScopeChoices::Route choice;
		choice.id = route.id;
		choice.corridor = route.corridor;
		for (const BlockSectionInput& block : route.blocks)
			choice.blockIds.push_back(block.id);
		choices.routes.push_back(std::move(choice));
	}
	return choices;
}

std::vector<BlockingTimePlannedReference> plannedReferencesFrom(const CompletedRunData& data,
	const BlockingTimeScope& scope, const std::map<int, RouteDiagramPath>& paths,
	const std::map<int, RouteDiagramProjection>& projections, int& omitted) {
	std::vector<BlockingTimePlannedReference> references;
	if (scope.routeIndex >= 0 && scope.blockIds.empty() && scope.trainIds.empty())
		return references; // A route without blocks and no train list has no stop in its scope.
	const auto selectedSections = data.hasRoute(scope.routeIndex) && !scope.sectionIndices.empty()
		? scopedBlockSectionExtents(buildBlockSectionExtents(data.routes[static_cast<std::size_t>(scope.routeIndex)].blocks), scope.sectionIndices)
		: std::vector<BlockSectionExtent>();
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
				: routeDiagramStopPosition(stop, path->second, projection->second);
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

std::optional<BlockingTimeDiagramData> blockingTimeDiagramData(const CompletedRunData& data,
	const BlockingTimeScope& scope) {
	// Empty filters keep every envelope, so a route without blocks and no train list keeps none itself.
	const std::vector<BlockingTimeDiagramSegment> scoped = scope.routeIndex >= 0 && scope.blockIds.empty() && scope.trainIds.empty()
		? std::vector<BlockingTimeDiagramSegment>()
		: filterBlockingTimeDiagramSegments(blockTimesFrom(data), scope.trainIds, scope.blockIds, scope.startTime,
			  scope.endTime);
	const int referenceIndex = scope.routeIndex >= 0 ? scope.routeIndex : firstRouteOfTrains(data, scope.trainIds);
	if (!data.hasRoute(referenceIndex))
		return std::nullopt;
	const CompletedRoute& referenceRoute = data.routes[static_cast<std::size_t>(referenceIndex)];

	BlockingTimeDiagramData result;
	result.referencePath = referenceRoute.path;
	result.timeRange = {scope.startTime, std::max(scope.endTime, scope.startTime + 1.0)};
	const RouteProjections maps = projectRoutes(data, result.referencePath);
	result.segments = projectBlockingSegments(scoped, maps.byTrain, result.omitted, &result.unmapped);
	const auto allExtents = buildBlockSectionExtents(referenceRoute.blocks);
	result.extents = scopedBlockSectionExtents(allExtents, scope.sectionIndices);
	result.routeSectionCount = static_cast<int>(allExtents.size());
	matchBlockingSegmentsToSections(result.segments, allExtents, result.referencePath.id);
	if (!scope.sectionIndices.empty())
		result.segments.erase(std::remove_if(result.segments.begin(), result.segments.end(),
								  [&result](const auto& segment) { return !blockingSegmentInSections(segment, result.extents); }),
			result.segments.end());
	const auto allPlanned = plannedReferencesFrom(data, scope, maps.paths, maps.projections, result.omitted);
	result.plannedReferences = filterBlockingTimePlannedReferences(allPlanned, scope.startTime, scope.endTime);
	result.plannedLines = clipBlockingTimePlannedReferences(allPlanned, scope.startTime, scope.endTime);
	// The envelopes are clipped to the interval of the scope, so the samples inside them are too.
	addRecordedTrajectories(result, data, maps.byTrain, {});
	return result;
}

std::optional<BlockingTimeDiagramData> compressedBlockingTimeDiagramData(const CompletedRunData& data,
	const CapacityAnalysisScope& scope, const CapacityAnalysisResult& result) {
	if (!data.hasRoute(scope.routeIndex))
		return std::nullopt;
	const CompletedRoute& route = data.routes[static_cast<std::size_t>(scope.routeIndex)];
	BlockingTimeDiagramData diagram;
	diagram.referencePath = route.path;
	RouteProjections maps = projectRoutes(data, diagram.referencePath);
	// The first train one period later lies on the route of the first train.
	const std::string nextPeriod = capacityNextPeriodIdentity(result.firstIdentity);
	if (scope.repeatFirstTrain
		&& std::find(result.trainIdentities.begin(), result.trainIdentities.end(), nextPeriod) != result.trainIdentities.end()) {
		diagram.repeatedTrains.emplace(nextPeriod, result.firstIdentity);
		const auto projection = maps.byTrain.find(result.firstIdentity);
		if (projection != maps.byTrain.end())
			maps.byTrain.emplace(nextPeriod, projection->second);
	}
	// A compressed occupation can start before 0 s, which validBlockingTimeDiagramInput takes for a missing time, so the
	// segments are built from the occupations moved to 0 s or later and then moved back.
	double lift = 0.0;
	for (const auto& occupations : result.compressedOccupations)
		for (const BlockingTimeDiagramInput& occupation : occupations)
			lift = std::max(lift, -occupation.startOccTime);
	auto lifted = result.compressedOccupations;
	for (auto& occupations : lifted)
		for (BlockingTimeDiagramInput& occupation : occupations) {
			occupation.startOccTime += lift;
			occupation.endOccTime += lift;
		}
	auto shifted = buildBlockingTimeDiagramSegments(lifted, result.trainIdentities);
	for (BlockingTimeDiagramSegment& segment : shifted) {
		segment.startTime -= lift;
		segment.endTime -= lift;
		segment.originalStartTime -= lift;
		segment.originalEndTime -= lift;
	}
	restoreCompressedOriginalTimes(shifted, result.compression);
	diagram.segments = projectBlockingSegments(shifted, maps.byTrain, diagram.omitted, &diagram.unmapped);
	const auto allExtents = buildBlockSectionExtents(route.blocks);
	diagram.extents = scopedBlockSectionExtents(allExtents, scope.sectionIndices);
	diagram.routeSectionCount = static_cast<int>(allExtents.size());
	// Every analysed occupation shares a resource with a block of the scope, so one that the route of its train does not
	// project is drawn on the blocks of the scope that share a resource with it.
	std::vector<BlockingTimeDiagramSegment> unplaced;
	for (BlockingTimeDiagramSegment& segment : diagram.unmapped) {
		if (placeOnSharedSections(segment, diagram.extents)) {
			diagram.segments.push_back(std::move(segment));
			--diagram.omitted;
		} else {
			unplaced.push_back(std::move(segment));
		}
	}
	diagram.unmapped = std::move(unplaced);
	matchBlockingSegmentsToSections(diagram.segments, allExtents, diagram.referencePath.id);
	// The samples move as the occupations do, the shift that restoreCompressedOriginalTimes takes back. Those of the first
	// train one period later are the samples of the first train, so they move from its profile reference.
	std::map<std::string, double> shifts;
	for (const CapacityCompressionRow& row : result.compression) {
		const bool repeated = diagram.repeatedTrains.count(row.identity) > 0;
		shifts.emplace(row.identity, row.compressedReference - (repeated ? result.compression.front() : row).originalReference);
	}
	addRecordedTrajectories(diagram, data, maps.byTrain, shifts);
	diagram.timeRange = {0.0, 1.0};
	if (!diagram.segments.empty()) {
		double first = diagram.segments.front().startTime;
		double last = diagram.segments.front().endTime;
		for (const auto& segment : diagram.segments) {
			first = std::min(first, segment.startTime);
			last = std::max(last, segment.endTime);
		}
		const double margin = std::max(1.0, 0.05 * (last - first));
		diagram.timeRange = {first >= 0.0 ? std::max(0.0, first - margin) : first - margin, last + margin};
	}
	return diagram;
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
	const RouteProjections maps = projectRoutes(data, result.referencePath);
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

std::optional<DelayDiagramData> delayDiagramData(const CompletedRunData& data, int referenceIndex) {
	if (!data.hasRoute(referenceIndex))
		return std::nullopt;
	DelayDiagramData result;
	result.referencePath = data.routes[static_cast<std::size_t>(referenceIndex)].path;
	if (result.referencePath.nodes.empty())
		return std::nullopt;
	// The positions of each station on the route in path order, once each: two sections share the node between them.
	std::map<std::string, std::vector<double>> stations;
	for (const RouteDiagramNode& node : result.referencePath.nodes) {
		if (node.stationName.empty() || !std::isfinite(node.positionKm))
			continue;
		std::vector<double>& positions = stations[node.stationName];
		if (std::find(positions.begin(), positions.end(), node.positionKm) == positions.end())
			positions.push_back(node.positionKm);
	}
	for (const CompletedTrain& train : data.trains) {
		std::vector<const TimetableResultRow*> calls; // at stations of the route, in journey order
		for (const TimetableResultRow& row : data.timetable)
			if (row.trainId == train.trainId && stations.count(row.stationId))
				calls.push_back(&row);
		if (calls.empty()) {
			result.withoutCalls.push_back(train.trainId);
			continue;
		}
		// The train moves up the route (1) or down it (-1) as between its last two calls that lie apart, and before
		// those as from its first to its last call at a station that the route passes once; 0 when neither says.
		std::vector<double> once;
		for (const TimetableResultRow* call : calls)
			if (const std::vector<double>& positions = stations.at(call->stationId); positions.size() == 1)
				once.push_back(positions.front());
		double direction = 0.0;
		if (once.size() >= 2 && once.front() != once.back())
			direction = once.back() > once.front() ? 1.0 : -1.0;
		DelayDiagramSeries series;
		series.trainId = train.trainId;
		std::optional<double> previous;
		for (const TimetableResultRow* call : calls) {
			const std::vector<double>& positions = stations.at(call->stationId);
			double x = positions.front();
			if (previous && direction != 0.0) {
				std::optional<double> ahead;
				for (const double position : positions)
					if ((position - *previous) * direction >= 0.0 && (!ahead || std::abs(position - *previous) < std::abs(*ahead - *previous)))
						ahead = position;
				x = ahead.value_or(x);
			}
			if (previous && x != *previous)
				direction = x > *previous ? 1.0 : -1.0;
			previous = x;
			if (call->arrivalDelaySeconds.available)
				series.points.push_back({x, call->arrivalDelaySeconds.value, call->stationId, call->journeyIndex, call->callIndex});
		}
		result.series.push_back(std::move(series));
	}
	return result;
}

QImage fullTableImage(const QTableView& table) {
	// A copy of the view on the same model with the sections of the table at their sizes but no scroll bars, so that
	// the width of the dock and the scrolling do not matter.
	QTableView full;
	full.setModel(table.model());
	full.setFont(table.font());
	full.setPalette(table.palette());
	full.setAlternatingRowColors(table.alternatingRowColors());
	full.setShowGrid(table.showGrid());
	full.setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	full.setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	full.horizontalHeader()->setStretchLastSection(false);
	full.horizontalHeader()->setFixedHeight(table.horizontalHeader()->height());
	full.verticalHeader()->setFixedWidth(table.verticalHeader()->width());
	for (int column = 0; column < table.model()->columnCount(); ++column)
		full.setColumnWidth(column, table.columnWidth(column));
	for (int row = 0; row < table.model()->rowCount(); ++row)
		full.setRowHeight(row, table.rowHeight(row));
	// A header paints its text in the font of its viewport. Under the application style sheet the first polish leaves that
	// viewport the application font when headers have a font of their own, such as the smaller one of macOS, and the text
	// then overflows the widths measured with the header font. Polishing the headers again gives their viewports that font.
	full.ensurePolished();
	for (QHeaderView* header : {full.horizontalHeader(), full.verticalHeader()}) {
		full.style()->unpolish(header);
		full.style()->polish(header);
	}
	const int frame = 2 * full.frameWidth();
	full.resize(full.verticalHeader()->width() + full.horizontalHeader()->length() + frame,
		full.horizontalHeader()->height() + full.verticalHeader()->length() + frame);
	return full.grab().toImage();
}
