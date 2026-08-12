#include "diagrams/CapacityAnalysis.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

bool isFiniteValue(double value) {
	return std::isfinite(value);
}

bool availableTime(double value) {
	return isFiniteValue(value) && value >= 0.0;
}

double scheduledReference(const CapacityAnalysisTrain& train, double profile) {
	return availableTime(train.scheduledReferenceTime) ? train.scheduledReferenceTime : profile;
}

CapacityPairRow pairConstraint(const CapacityAnalysisTrain& leader,
	const CapacityAnalysisTrain& follower, double leaderProfile, double followerProfile,
	bool adjacent) {
	CapacityPairRow row;
	row.leaderIdentity = leader.runtimeId;
	row.followerIdentity = follower.runtimeId;
	row.leaderOperatingCode = leader.operatingCode;
	row.followerOperatingCode = follower.operatingCode;
	row.adjacent = adjacent;
	if (!availableTime(leaderProfile) || !availableTime(followerProfile))
		return row;
	const double leaderScheduled = scheduledReference(leader, leaderProfile);
	const double followerScheduled = scheduledReference(follower, followerProfile);
	if (availableTime(leaderScheduled) && availableTime(followerScheduled)) {
		row.scheduledHeadway = followerScheduled - leaderScheduled;
	}

	double maximumCandidate = -std::numeric_limits<double>::infinity();
	for (const BlockingTimeDiagramInput& leaderOccupation : leader.occupations) {
		if (!validBlockingTimeDiagramInput(leaderOccupation))
			continue;
		for (const BlockingTimeDiagramInput& followerOccupation : follower.occupations) {
			if (!validBlockingTimeDiagramInput(followerOccupation)
				|| !shareBlockingTimeResource(leaderOccupation, followerOccupation))
				continue;
			const double leaderOffset = leaderOccupation.endOccTime - leaderProfile;
			const double followerOffset = followerOccupation.startOccTime - followerProfile;
			const double candidate = leaderOffset - followerOffset;
			if (!isFiniteValue(candidate))
				continue;
			if (!row.hasSharedConstraint || candidate > maximumCandidate + kBlockingTimeToleranceSeconds) {
				row.governingEvidence.clear();
				maximumCandidate = candidate;
				row.hasSharedConstraint = true;
			} else if (std::abs(candidate - maximumCandidate) > kBlockingTimeToleranceSeconds) {
				continue;
			}
			if (row.hasSharedConstraint && std::abs(candidate - maximumCandidate) <= kBlockingTimeToleranceSeconds)
				row.governingEvidence.push_back({leaderOccupation.blockId, followerOccupation.blockId,
					leaderOffset, followerOffset, candidate});
		}
	}
	if (row.hasSharedConstraint) {
		row.minimumHeadway = std::max(0.0, maximumCandidate);
		if (isFiniteValue(row.scheduledHeadway))
			row.buffer = row.scheduledHeadway - row.minimumHeadway;
	}
	return row;
}

bool conflicts(const std::vector<std::vector<BlockingTimeDiagramInput>>& occupations) {
	for (std::size_t leaderIndex = 0; leaderIndex < occupations.size(); ++leaderIndex) {
		for (std::size_t followerIndex = leaderIndex + 1; followerIndex < occupations.size(); ++followerIndex) {
			for (const auto& leader : occupations[leaderIndex]) {
				if (!validBlockingTimeDiagramInput(leader))
					continue;
				for (const auto& follower : occupations[followerIndex]) {
					if (!validBlockingTimeDiagramInput(follower) || !shareBlockingTimeResource(leader, follower))
						continue;
					if (std::max(leader.startOccTime, follower.startOccTime)
						< std::min(leader.endOccTime, follower.endOccTime) - kBlockingTimeToleranceSeconds)
						return true;
				}
			}
		}
	}
	return false;
}

} // namespace

CapacityAnalysisResult analyzeCapacity(const std::vector<CapacityAnalysisTrain>& trains,
	 double periodSeconds, const std::string& cycleEndIdentity) {
	CapacityAnalysisResult result;
	result.periodSeconds = availableTime(periodSeconds) ? periodSeconds : -1.0;
	result.trainIdentities.reserve(trains.size());
	result.referenceLabels.reserve(trains.size());
	result.referenceSources.reserve(trains.size());
	for (const CapacityAnalysisTrain& train : trains) {
		result.trainIdentities.push_back(train.runtimeId);
		result.referenceLabels.push_back(train.referenceLabel);
		result.referenceSources.push_back(train.referenceSource);
	}
	if (trains.empty())
		return result;

	std::vector<double> profileReferences;
	std::vector<double> scheduledReferences;
	profileReferences.reserve(trains.size());
	scheduledReferences.reserve(trains.size());
	for (const CapacityAnalysisTrain& train : trains) {
		const double profile = train.profileReferenceTime;
		profileReferences.push_back(profile);
		scheduledReferences.push_back(scheduledReference(train, profile));
	}

	std::vector<std::vector<CapacityPairRow>> pairMatrix(trains.size());
	for (std::size_t leaderIndex = 0; leaderIndex < trains.size(); ++leaderIndex) {
		pairMatrix[leaderIndex].resize(trains.size());
		for (std::size_t followerIndex = leaderIndex + 1; followerIndex < trains.size(); ++followerIndex)
		{
			CapacityPairRow pair = pairConstraint(trains[leaderIndex], trains[followerIndex],
				profileReferences[leaderIndex], profileReferences[followerIndex],
				followerIndex == leaderIndex + 1);
			pairMatrix[leaderIndex][followerIndex] = pair;
			if (pair.adjacent)
				result.pairs.push_back(pair);
			result.allPairs.push_back(std::move(pair));
		}
	}

	std::vector<double> compressedReferences(trains.size(), -1.0);
	result.compression.reserve(trains.size());
	for (std::size_t index = 0; index < trains.size(); ++index) {
		CapacityCompressionRow row;
		row.identity = trains[index].runtimeId;
		row.operatingCode = trains[index].operatingCode;
		row.originalReference = profileReferences[index];
		row.scheduledReference = scheduledReferences[index];
		row.compressedReference = scheduledReferences[index];
		double compressed = scheduledReferences[index];
		if (index > 0) {
			compressed = compressedReferences[index - 1];
			double best = compressed;
			for (std::size_t predecessor = 0; predecessor < index; ++predecessor) {
				const CapacityPairRow& pair = pairMatrix[predecessor][index];
				if (!pair.hasSharedConstraint)
					continue;
				const double candidate = compressedReferences[predecessor] + pair.minimumHeadway;
				if (!isFiniteValue(candidate))
					continue;
				if (!isFiniteValue(best) || candidate > best + kBlockingTimeToleranceSeconds) {
					best = candidate;
					row.governingPredecessors.clear();
				} else if (std::abs(candidate - best) > kBlockingTimeToleranceSeconds) {
					continue;
				}
				if (std::abs(candidate - best) <= kBlockingTimeToleranceSeconds)
					row.governingPredecessors.push_back({trains[predecessor].runtimeId,
						pair.minimumHeadway, pair.governingEvidence});
			}
			compressed = best;
		}
		row.compressedReference = compressed;
		row.shift = isFiniteValue(compressed) && isFiniteValue(row.scheduledReference)
			? compressed - row.scheduledReference : 0.0;
		compressedReferences[index] = compressed;
		result.compression.push_back(std::move(row));
	}

	result.compressedOccupations.resize(trains.size());
	for (std::size_t index = 0; index < trains.size(); ++index) {
		const double shift = availableTime(compressedReferences[index]) && availableTime(profileReferences[index])
			? compressedReferences[index] - profileReferences[index] : 0.0;
		for (const BlockingTimeDiagramInput& source : trains[index].occupations) {
			if (!validBlockingTimeDiagramInput(source))
				continue;
			BlockingTimeDiagramInput shifted = source;
			shifted.startOccTime += shift;
			shifted.endOccTime += shift;
			result.compressedOccupations[index].push_back(std::move(shifted));
		}
	}

	for (std::size_t followerIndex = 1; followerIndex < result.compressedOccupations.size(); ++followerIndex) {
		for (const CapacityCompressionEvidence& governing : result.compression[followerIndex].governingPredecessors) {
			const auto predecessor = std::find_if(trains.begin(), trains.end(), [&governing](const CapacityAnalysisTrain& train) {
				return train.runtimeId == governing.predecessorIdentity;
			});
			if (predecessor == trains.end())
				continue;
			const std::size_t leaderIndex = static_cast<std::size_t>(predecessor - trains.begin());
			for (std::size_t leaderOccupation = 0;
				 leaderOccupation < result.compressedOccupations[leaderIndex].size(); ++leaderOccupation) {
				auto& leader = result.compressedOccupations[leaderIndex][leaderOccupation];
				for (std::size_t followerOccupation = 0;
					 followerOccupation < result.compressedOccupations[followerIndex].size(); ++followerOccupation) {
					auto& follower = result.compressedOccupations[followerIndex][followerOccupation];
					if (!shareBlockingTimeResource(leader, follower))
						continue;
					const double gap = follower.startOccTime - leader.endOccTime;
					if (std::abs(gap) > kBlockingTimeToleranceSeconds)
						continue;
					leader.capacityCritical = true;
					follower.capacityCritical = true;
					result.criticalBlocks.push_back({trains[leaderIndex].runtimeId,
						trains[followerIndex].runtimeId, leader.blockId, follower.blockId, gap});
				}
			}
		}
	}

	result.conflictFree = !conflicts(result.compressedOccupations);
	const bool hasValidProfiles = std::all_of(profileReferences.begin(), profileReferences.end(), availableTime);
	const bool hasAdjacentConstraints = !result.pairs.empty()
		&& std::all_of(result.pairs.begin(), result.pairs.end(),
			[](const CapacityPairRow& pair) { return pair.hasSharedConstraint; });
	result.analyzable = trains.size() >= 2 && hasValidProfiles && hasAdjacentConstraints && result.conflictFree;
	if (result.compression.empty())
		return result;
	result.firstIdentity = result.compression.front().identity;
	const auto cycleEnd = std::find_if(result.compression.begin(), result.compression.end(),
		[&cycleEndIdentity](const CapacityCompressionRow& row) { return row.identity == cycleEndIdentity; });
	if (cycleEnd == result.compression.end() || cycleEnd == result.compression.begin())
		return result;
	result.cycleEndIdentity = cycleEnd->identity;
	if (isFiniteValue(result.compression.front().compressedReference) && isFiniteValue(cycleEnd->compressedReference)) {
		result.cycleTime = cycleEnd->compressedReference - result.compression.front().compressedReference;
		if (isFiniteValue(result.periodSeconds) && result.periodSeconds > 0.0)
			result.cyclePercentage = result.cycleTime / result.periodSeconds * 100.0;
	}
	return result;
}
