#include "diagrams/AreaCapacity.h"
#include "diagrams/BlockingTimeDiagram.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <set>
#include <utility>

namespace {

// A kept train of one list: its position in the input list and its
// occupations that touch the area.
struct TrainUse {
	std::size_t index = 0;
	// At least one occupation touches the area, usable or not.
	bool touches = false;
	// The occupations that touch the area and pass validBlockingTimeDiagramInput, in input order.
	std::vector<BlockingTimeDiagramInput> usable;
};

bool validPeriod(double periodSeconds) {
	return std::isfinite(periodSeconds) && periodSeconds > 0.0;
}

void addNote(std::vector<AreaCapacityNote>& notes, AreaCapacityNoteKind kind, const std::string& trainId = {},
	const std::string& otherTrainId = {}, const std::string& resourceId = {}) {
	notes.push_back({kind, trainId, otherTrainId, resourceId});
}

bool hasNote(const std::vector<AreaCapacityNote>& notes, AreaCapacityNoteKind kind) {
	return std::any_of(notes.begin(), notes.end(), [kind](const AreaCapacityNote& note) { return note.kind == kind; });
}

std::vector<std::string> distinctResources(const std::vector<std::string>& ids) {
	std::vector<std::string> resources;
	for (const std::string& id : ids) {
		if (id.empty() || id == "None" || std::find(resources.begin(), resources.end(), id) != resources.end())
			continue;
		resources.push_back(id);
	}
	return resources;
}

// The positions of the trains whose identity is new. A repeated identity gets
// a note. `seen` carries the identities over from one list to the next.
std::vector<std::size_t> keepFirstIdentities(const std::vector<AreaCapacityTrain>& trains,
	std::set<std::string>& seen, std::vector<AreaCapacityNote>& notes) {
	std::vector<std::size_t> kept;
	for (std::size_t index = 0; index < trains.size(); ++index) {
		const std::string& id = trains[index].train.runtimeId;
		if (seen.insert(id).second)
			kept.push_back(index);
		else
			addNote(notes, AreaCapacityNoteKind::DuplicateIdentity, id);
	}
	return kept;
}

// The position of the first resource that the block id shares a component
// with, or resources.size() when it touches none.
std::size_t firstTouchedResource(const std::string& blockId, const std::vector<std::string>& resources) {
	std::size_t index = 0;
	while (index < resources.size() && !shareBlockingTimeResource(blockId, resources[index]))
		++index;
	return index;
}

TrainUse useOfTrain(const AreaCapacityTrain& source, std::size_t index, const std::vector<std::string>& resources,
	std::vector<AreaCapacityNote>& notes) {
	TrainUse use;
	use.index = index;
	std::vector<bool> reported(resources.size(), false);
	for (const BlockingTimeDiagramInput& occupation : source.train.occupations) {
		const std::size_t resource = firstTouchedResource(occupation.blockId, resources);
		if (resource == resources.size())
			continue;
		use.touches = true;
		if (validBlockingTimeDiagramInput(occupation)) {
			use.usable.push_back(occupation);
		} else if (!reported[resource]) {
			reported[resource] = true;
			addNote(notes, AreaCapacityNoteKind::IncompleteOccupation, source.train.runtimeId, {}, resources[resource]);
		}
	}
	return use;
}

// Length of the union of the intervals (start, end); overlapping or touching intervals merge.
double unionLength(std::vector<std::pair<double, double>> intervals) {
	std::sort(intervals.begin(), intervals.end());
	double total = 0.0;
	std::size_t index = 0;
	while (index < intervals.size()) {
		const double start = intervals[index].first;
		double end = intervals[index].second;
		for (++index; index < intervals.size() && intervals[index].first <= end; ++index)
			end = std::max(end, intervals[index].second);
		total += end - start;
	}
	return total;
}

// The row of one resource, without percentage and governing flag.
AreaCapacityResource resourceRow(const std::string& resourceId, const std::vector<TrainUse>& uses,
	const std::vector<AreaCapacityTrain>& trains, std::vector<AreaCapacityNote>& notes) {
	AreaCapacityResource row;
	row.resourceId = resourceId;
	std::vector<std::pair<double, double>> intervals;
	for (const TrainUse& use : uses) {
		int count = 0;
		for (const BlockingTimeDiagramInput& occupation : use.usable) {
			if (!shareBlockingTimeResource(occupation.blockId, resourceId))
				continue;
			++count;
			row.occupationSeconds += occupation.endOccTime - occupation.startOccTime;
			intervals.emplace_back(occupation.startOccTime, occupation.endOccTime);
		}
		if (count == 0)
			continue;
		++row.trainCount;
		row.occupationCount += count;
		if (count > 1)
			addNote(notes, AreaCapacityNoteKind::RepeatedResource, trains[use.index].train.runtimeId, {}, resourceId);
	}
	row.unionSeconds = unionLength(intervals);
	return row;
}

// Sets the chain and the notes of the chain from the selected trains.
void analyzeChain(const AreaCapacityInput& input, const std::vector<TrainUse>& uses, AreaCapacityResult& result) {
	if (uses.empty()) {
		addNote(result.notes, AreaCapacityNoteKind::NoSelectedTraffic);
		result.chain = analyzeCapacity({}, result.periodSeconds, input.cycleEndIdentity);
		return;
	}

	const bool chainDirection = input.selected[uses.front().index].reversedDirection;
	std::vector<CapacityAnalysisTrain> chainTrains;
	for (const TrainUse& use : uses) {
		const AreaCapacityTrain& source = input.selected[use.index];
		if (source.reversedDirection != chainDirection) {
			addNote(result.notes, AreaCapacityNoteKind::OpposingTrain, source.train.runtimeId);
			continue;
		}
		CapacityAnalysisTrain chainTrain = source.train;
		chainTrain.occupations = use.usable;
		chainTrains.push_back(std::move(chainTrain));
	}
	result.chain = analyzeCapacity(chainTrains, result.periodSeconds, input.cycleEndIdentity);

	std::vector<std::string> invalidReferences;
	for (const CapacityAnalysisTrain& chainTrain : chainTrains) {
		if (std::isfinite(chainTrain.profileReferenceTime) && chainTrain.profileReferenceTime >= 0.0)
			continue;
		invalidReferences.push_back(chainTrain.runtimeId);
		addNote(result.notes, AreaCapacityNoteKind::InvalidReference, chainTrain.runtimeId);
	}
	if (chainTrains.size() == 1) {
		addNote(result.notes, AreaCapacityNoteKind::FewerThanTwoTrains);
		return;
	}

	const auto invalidReference = [&invalidReferences](const std::string& id) {
		return std::find(invalidReferences.begin(), invalidReferences.end(), id) != invalidReferences.end();
	};
	for (const CapacityPairRow& pair : result.chain.pairs) {
		if (!pair.hasSharedConstraint && !invalidReference(pair.leaderIdentity) && !invalidReference(pair.followerIdentity))
			addNote(result.notes, AreaCapacityNoteKind::NoSharedResource, pair.leaderIdentity, pair.followerIdentity);
	}
	if (!result.chain.conflictFree)
		addNote(result.notes, AreaCapacityNoteKind::ChainConflict);
}

} // namespace

AreaCapacityResult analyzeAreaCapacity(const AreaCapacityInput& input) {
	AreaCapacityResult result;
	const bool periodValid = validPeriod(input.periodSeconds);
	if (periodValid)
		result.periodSeconds = input.periodSeconds;
	else
		addNote(result.notes, AreaCapacityNoteKind::InvalidPeriod);

	const std::vector<std::string> resources = distinctResources(input.resourceIds);
	if (resources.empty()) {
		addNote(result.notes, AreaCapacityNoteKind::EmptyScope);
		return result;
	}

	std::set<std::string> seenIdentities;
	const std::vector<std::size_t> keptSelected = keepFirstIdentities(input.selected, seenIdentities, result.notes);
	const std::vector<std::size_t> keptOther = keepFirstIdentities(input.otherTraffic, seenIdentities, result.notes);

	std::vector<TrainUse> selectedUses;
	for (const std::size_t index : keptSelected)
		selectedUses.push_back(useOfTrain(input.selected[index], index, resources, result.notes));
	std::vector<TrainUse> otherUses;
	for (const std::size_t index : keptOther)
		otherUses.push_back(useOfTrain(input.otherTraffic[index], index, resources, result.notes));

	double largestSeconds = 0.0;
	for (const std::string& resourceId : resources) {
		AreaCapacityResource row = resourceRow(resourceId, selectedUses, input.selected, result.notes);
		if (periodValid)
			row.occupationPercentage = row.occupationSeconds / input.periodSeconds * 100.0;
		largestSeconds = std::max(largestSeconds, row.occupationSeconds);
		result.resources.push_back(std::move(row));
	}
	for (AreaCapacityResource& row : result.resources)
		row.governing = row.occupationSeconds > 0.0 && row.occupationSeconds >= largestSeconds - kBlockingTimeToleranceSeconds;

	bool hasOccupation = false;
	double earliestStart = 0.0;
	double latestEnd = 0.0;
	for (const TrainUse& use : selectedUses) {
		for (const BlockingTimeDiagramInput& occupation : use.usable) {
			earliestStart = hasOccupation ? std::min(earliestStart, occupation.startOccTime) : occupation.startOccTime;
			latestEnd = hasOccupation ? std::max(latestEnd, occupation.endOccTime) : occupation.endOccTime;
			hasOccupation = true;
		}
	}
	if (hasOccupation) {
		result.envelopeSeconds = latestEnd - earliestStart;
		if (periodValid)
			result.envelopePercentage = result.envelopeSeconds / input.periodSeconds * 100.0;
	}

	analyzeChain(input, selectedUses, result);

	for (const TrainUse& use : otherUses) {
		if (use.touches)
			addNote(result.notes, AreaCapacityNoteKind::UnselectedTraffic, input.otherTraffic[use.index].train.runtimeId);
	}
	const bool anySelectedTrain = std::any_of(result.resources.begin(), result.resources.end(),
		[](const AreaCapacityResource& row) { return row.trainCount > 0; });
	result.complete = anySelectedTrain && !hasNote(result.notes, AreaCapacityNoteKind::UnselectedTraffic)
		&& !hasNote(result.notes, AreaCapacityNoteKind::IncompleteOccupation)
		&& !hasNote(result.notes, AreaCapacityNoteKind::DuplicateIdentity);
	return result;
}
