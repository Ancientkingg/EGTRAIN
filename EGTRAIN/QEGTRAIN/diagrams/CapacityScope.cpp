#include "diagrams/CapacityScope.h"

#include "diagrams/RouteDiagramCoordinates.h"
#include "diagrams/SimulationHeaders.h"

#include <algorithm>
#include <cmath>

namespace {

BlockingTimeDiagramInput capacityOccupation(const BlockingTimes& source) {
	BlockingTimeDiagramInput occupation;
	occupation.blockId = source.BlockID;
	occupation.startOccTime = source.StartOccTime;
	occupation.endOccTime = source.EndOccTime;
	occupation.posStart = source.PosStart;
	occupation.posEnd = source.PosEnd;
	occupation.switchName = source.SwitchName;
	occupation.stationName = source.stationName;
	occupation.isComplete = source.IsComplete;
	occupation.startApproachTime = source.StartApproachTime;
	occupation.startRunTime = source.StartRunTime;
	occupation.endRunTime = source.EndRunTime;
	occupation.endClearTime = source.EndClearTime;
	occupation.setupTime = source.setupTime;
	occupation.sightReactionTime = source.sightReacTime;
	occupation.releaseTime = source.ReleaseTime;
	occupation.runTimeMargin = source.RunTimeMargin;
	return occupation;
}

} // namespace

// The capacity calculation is resource-id based. Refuse only the selected ambiguity,
// rather than choosing one route occurrence or changing the capacity mathematics.
std::string capacityScopeIssue(const CapacityAnalysisScope& scope) {
	if (scope.routeIndex < 0 || scope.routeIndex >= static_cast<int>(train_route.size()) || scope.blockIds.empty()) return {};
	const auto extents = buildBlockSectionExtents(blockSectionInputsForRoute(train_route[scope.routeIndex]));
	const std::string issue = blockSectionCapacityScopeIssue(extents, scope.sectionIndices);
	if (!issue.empty()) return issue;
	for (int index = 0; index < numRegions; ++index) {
		const Train& train = regional_train[index];
		if (!scope.occurrenceIds.empty() && std::find(scope.occurrenceIds.begin(), scope.occurrenceIds.end(), train.trainDescription) == scope.occurrenceIds.end()) continue;
		std::vector<BlockingTimeDiagramInput> occupations;
		for (int block = 0; block < train.N_BlockSections; ++block) occupations.push_back(capacityOccupation(train.BlockTime[block]));
		if (blockingTimeEntryAmbiguous(occupations, scope.blockIds.front()))
			return "Occurrence " + train.trainDescription + " has several records for the selected entry resource; capacity analysis cannot identify one entry.";
	}
	return {};
}

CapacityAnalysisTrain capacityTrainForScope(const Train& train, const CapacityAnalysisScope& scope) {
	CapacityAnalysisTrain result;
	result.runtimeId = train.trainDescription;
	result.operatingCode = train.operatingCode;
	if (scope.blockIds.empty())
		return result;

	const BlockingTimes* reference = nullptr;
	for (int blockIndex = 0; blockIndex < train.N_BlockSections; ++blockIndex) {
		const BlockingTimes& source = train.BlockTime[blockIndex];
		const BlockingTimeDiagramInput occupation = capacityOccupation(source);
		if (!validBlockingTimeDiagramInput(occupation))
			continue;
		if (!reference && shareBlockingTimeResource(occupation.blockId, scope.blockIds.front()))
			reference = &source;
		for (const std::string& selectedBlock : scope.blockIds) {
			if (shareBlockingTimeResource(occupation.blockId, selectedBlock)) {
				result.occupations.push_back(occupation);
				break;
			}
		}
	}
	if (!reference || !std::isfinite(reference->StartRunTime) || reference->StartRunTime < 0.0) {
		result.occupations.clear();
		return result;
	}
	result.profileReferenceTime = reference->StartRunTime;
	result.referenceLabel = "block " + scope.blockIds.front() + " entry";
	result.referenceSource = "selected section StartRunTime";

	// A boundary stop is the authored reference when its route position matches
	// the selected section entry. The 5 m tolerance is the native route tolerance.
	if (!train.Stations.empty() && reference->PosStart >= 0.0) {
		const int stationCount = std::min(train.numStations, static_cast<int>(Train::kMaxTimetableStations));
		for (int stationIndex = 0; stationIndex < stationCount; ++stationIndex) {
			const double stationPosition = train.stationRoutePositionMeters(stationIndex);
			if (!std::isfinite(stationPosition) || std::abs(stationPosition - reference->PosStart) > 5.0)
				continue;
			const double departure = train.ScheduledDepartures[stationIndex];
			const double arrival = train.ScheduledArrivals[stationIndex];
			const double authored = departure >= 0.0 ? departure : arrival;
			if (!std::isfinite(authored) || authored < 0.0)
				continue;
			const std::string station = train.stationNameForArrivalStats(stationIndex);
			result.scheduledReferenceTime = authored;
			result.referenceLabel = station + " boundary";
			result.referenceSource = departure >= 0.0
				? "authored ScheduledDeparture at boundary station"
				: "authored ScheduledArrival at boundary station (departure absent)";
			return result;
		}
	}

	if (std::isfinite(train.scheduled_departure_time) && train.scheduled_departure_time >= 0.0
		&& train.RunStartTime >= 0) {
		result.scheduledReferenceTime = train.scheduled_departure_time
			+ (reference->StartRunTime - static_cast<double>(train.RunStartTime));
		result.referenceSource = "canonical scheduled entry + elapsed from RunStartTime";
	} else {
		result.scheduledReferenceTime = result.profileReferenceTime;
		result.referenceSource = "profile reference fallback (scheduled entry unavailable)";
	}
	return result;
}

std::vector<CapacityAnalysisTrain> capacityTrainsForScope(const CapacityAnalysisScope& scope) {
	std::vector<CapacityAnalysisTrain> candidates;
	for (int trainIndex = 0; trainIndex < numRegions; ++trainIndex) {
		const CapacityAnalysisTrain candidate = capacityTrainForScope(regional_train[trainIndex], scope);
		if (candidate.occupations.empty())
			continue;
		if (!scope.occurrenceIds.empty() && std::find(scope.occurrenceIds.begin(), scope.occurrenceIds.end(), candidate.runtimeId) == scope.occurrenceIds.end())
			continue;
		candidates.push_back(candidate);
	}
	if (scope.occurrenceIds.empty())
		return candidates;
	std::vector<CapacityAnalysisTrain> trains;
	trains.reserve(scope.occurrenceIds.size());
	for (const std::string& occurrenceId : scope.occurrenceIds) {
		const auto it = std::find_if(candidates.begin(), candidates.end(),
			[&occurrenceId](const CapacityAnalysisTrain& candidate) {
				return candidate.runtimeId == occurrenceId;
			});
		if (it != candidates.end())
			trains.push_back(*it);
	}
	return trains;
}

CapacityAnalysisScope firstCapacityScopeWithPair() {
	CapacityAnalysisScope empty;
	for (std::size_t routeIndex = 0; routeIndex < train_route.size(); ++routeIndex) {
		const Route& route = train_route[routeIndex];
		for (int blockIndex = 0; blockIndex < route.N_Block_Sections; ++blockIndex) {
			CapacityAnalysisScope candidate;
			candidate.routeIndex = static_cast<int>(routeIndex);
			candidate.blockIds.push_back(route.sequence_of_block_sections[blockIndex].ID);
			candidate.sectionIndices.push_back(blockIndex);
			const auto trains = capacityTrainsForScope(candidate);
			if (trains.size() < 2)
				continue;
			for (const auto& train : trains)
				candidate.occurrenceIds.push_back(train.runtimeId);
			return candidate;
		}
	}
	return empty;
}

QString capacityScopeLabel(const CapacityAnalysisScope& scope) {
	if (scope.routeIndex < 0 || scope.routeIndex >= static_cast<int>(train_route.size()))
		return QStringLiteral("no selected route");
	const Route& route = train_route[static_cast<std::size_t>(scope.routeIndex)];
	QString label = QString::fromStdString(route.ID);
	if (!route.corridor.empty())
		label += QString(" / %1").arg(QString::fromStdString(route.corridor));
	if (!scope.blockIds.empty())
		label += QString(" / %1 to %2").arg(QString::fromStdString(scope.blockIds.front()), QString::fromStdString(scope.blockIds.back()));
	return label;
}
