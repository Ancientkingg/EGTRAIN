#include "diagrams/CapacityAnalysis.h"

#include <algorithm>
#include <cmath>
#include <iostream>

namespace {

bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

BlockingTimeDiagramInput occupation(const char* id, double start, double end) {
	BlockingTimeDiagramInput value;
	value.blockId = id;
	value.startOccTime = start;
	value.endOccTime = end;
	value.posStart = 0.0;
	value.posEnd = 100.0;
	value.isComplete = true;
	return value;
}

CapacityAnalysisTrain train(const char* id, double scheduled,
	std::initializer_list<BlockingTimeDiagramInput> occupations) {
	CapacityAnalysisTrain value;
	value.runtimeId = id;
	value.operatingCode = id;
	value.profileReferenceTime = 0.0;
	value.scheduledReferenceTime = scheduled;
	value.referenceLabel = id;
	value.referenceSource = "test";
	value.occupations = occupations;
	return value;
}

} // namespace

int main() {
	bool ok = true;
	const auto leader = train("A", 0.0, {occupation("2-B0", 0.0, 20.0)});
	const auto follower = train("B", 20.0, {occupation("2-B0", 5.0, 25.0)});
	const auto pair = analyzeCapacity({leader, follower}, 3600.0);
	ok &= expect(pair.pairs.size() == 1, "one adjacent pair row");
	ok &= expect(pair.pairs[0].hasSharedConstraint, "shared pair is valid");
	ok &= expect(pair.pairs[0].minimumHeadway == 15.0, "governing minimum headway uses offsets");
	ok &= expect(pair.pairs[0].buffer == 5.0, "scheduled buffer is not clamped");
	ok &= expect(pair.pairs[0].governingEvidence.size() == 1
		&& pair.pairs[0].governingEvidence[0].leaderBlockId == "2-B0"
		&& pair.pairs[0].governingEvidence[0].followerBlockId == "2-B0",
		"headway retains governing block evidence");
	const auto tied = analyzeCapacity({train("A", 0.0,
		{occupation("AB", 0.0, 20.0), occupation("AC", 0.0, 20.0)}),
		train("B", 20.0, {occupation("AB", 5.0, 25.0), occupation("AC", 5.0, 25.0)})});
	ok &= expect(tied.pairs[0].governingEvidence.size() == 2,
		"tied governing resources are all retained");

	const auto negativeBuffer = analyzeCapacity({leader,
		train("B", 10.0, {occupation("2-B0", 5.0, 25.0)})});
	ok &= expect(negativeBuffer.pairs[0].buffer == -5.0, "negative scheduled buffer is retained");
	ok &= expect(shareBlockingTimeResource("@2-B0@-1.0/@3-B1@-2.0", "2-B0"),
		"composite block matches its exact component");
	ok &= expect(!shareBlockingTimeResource("2-B0", "2-B4"),
		"distinct decorated block resources do not match");
	const auto noShared = analyzeCapacity({leader,
		train("B", 20.0, {occupation("other", 5.0, 25.0)})});
	ok &= expect(!noShared.pairs[0].hasSharedConstraint && noShared.pairs[0].minimumHeadway < 0.0,
		"no shared resource is invalid");
	CapacityAnalysisTrain malformed = follower;
	malformed.profileReferenceTime = -1.0;
	ok &= expect(!analyzeCapacity({leader, malformed}).analyzable,
		"missing profile reference is not analyzable");
	const auto zeroDuration = analyzeCapacity({leader,
		train("B", 20.0, {occupation("2-B0", 5.0, 5.0)})});
	ok &= expect(!zeroDuration.pairs[0].hasSharedConstraint,
		"zero-duration occupations are rejected consistently with the diagram");
	const auto nearTouch = analyzeCapacity({
		train("A", 0.0, {occupation("G", 0.0, 20.0), occupation("X", 0.0, 20.00000005)}),
		train("B", 20.0, {occupation("G", 5.0, 25.0), occupation("X", 5.0, 25.0)})},
		100.0, "B");
	ok &= expect(nearTouch.conflictFree && nearTouch.criticalBlocks.size() == 2,
		"touch tolerance is not also classified as an overlap conflict");

	const std::vector<CapacityAnalysisTrain> sequence = {
		train("A", 0.0, {occupation("AB", 0.0, 20.0), occupation("AC", 0.0, 100.0)}),
		train("B", 100.0, {occupation("AB", 0.0, 10.0), occupation("BC", 0.0, 5.0)}),
		train("C", 30.0, {occupation("AC", 0.0, 50.0), occupation("BC", 0.0, 5.0),
			occupation("CD", 0.0, 10.0)}),
		train("D", 120.0, {occupation("CD", 0.0, 5.0)})};
	const auto before = sequence;
	const auto compressed = analyzeCapacity(sequence, 200.0, "C");
	ok &= expect(sequence[0].occupations[0].startOccTime == before[0].occupations[0].startOccTime
		&& sequence[2].scheduledReferenceTime == before[2].scheduledReferenceTime,
		"capacity analysis does not mutate inputs");
	ok &= expect(compressed.compression.size() == 4
		&& compressed.compression[1].compressedReference == 20.0
		&& compressed.compression[2].compressedReference == 100.0
		&& compressed.compression[3].compressedReference == 110.0,
		"compression considers every earlier train");
	ok &= expect(compressed.compression[2].governingPredecessors.size() == 1
		&& compressed.compression[2].governingPredecessors[0].predecessorIdentity == "A",
		"nonadjacent predecessor governs compression");
	ok &= expect(compressed.conflictFree, "compressed occupations have no overlaps");
	const auto hasCritical = [&compressed](const char* leaderId, const char* followerId, const char* blockId) {
		return std::any_of(compressed.criticalBlocks.begin(), compressed.criticalBlocks.end(),
			[=](const CapacityCriticalBlock& critical) {
				return critical.leaderIdentity == leaderId && critical.followerIdentity == followerId
					&& critical.leaderBlockId == blockId && std::abs(critical.gap) < 1e-9;
			});
	};
	ok &= expect(compressed.criticalBlocks.size() == 3 && hasCritical("A", "B", "AB")
		&& hasCritical("A", "C", "AC") && hasCritical("C", "D", "CD"),
		"adjacent and nonadjacent governing touches are retained separately");
	const auto compressedSegments = buildBlockingTimeDiagramSegments(compressed.compressedOccupations,
		compressed.trainIdentities);
	const auto criticalSegment = std::find_if(compressedSegments.begin(), compressedSegments.end(),
		[](const BlockingTimeDiagramSegment& segment) {
			return segment.capacityCritical && segment.blockId == "AB";
		});
	ok &= expect(criticalSegment != compressedSegments.end()
		&& criticalSegment->style != BlockingTimeSegmentStyle::Critical,
		"touching capacity-critical styling stays distinct from red overlap conflict");
	ok &= expect(compressed.cycleTime == 100.0 && compressed.cyclePercentage == 50.0,
		"explicit cycle endpoint and transparent period percentage are exact");
	ok &= expect(compressed.firstIdentity == "A" && compressed.cycleEndIdentity == "C"
		&& compressed.compression.back().identity == "D",
		"cycle endpoint is explicit and independent of the last selected train");
	const auto missingCycleEnd = analyzeCapacity(sequence, 200.0);
	ok &= expect(missingCycleEnd.cycleEndIdentity.empty() && missingCycleEnd.cycleTime < 0.0,
		"cycle consumption is unavailable without an explicit closing occurrence");

	if (!ok)
		return 1;
	std::cout << "all CapacityAnalysis tests passed\n";
	return 0;
}
