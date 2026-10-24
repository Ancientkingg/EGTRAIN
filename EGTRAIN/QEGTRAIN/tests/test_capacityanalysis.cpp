#include "diagrams/CapacityAnalysis.h"
#include "diagrams/CapacityScope.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

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
	value.endClearTime = end;
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

// A train of the order tests: its profile reference and its occupations.
CapacityAnalysisTrain entering(const char* id, double profile, std::initializer_list<BlockingTimeDiagramInput> occupations) {
	CapacityAnalysisTrain value = train(id, profile, occupations);
	value.profileReferenceTime = profile;
	return value;
}

std::string idsOf(const std::vector<CapacityAnalysisTrain>& trains) {
	std::string text;
	for (const CapacityAnalysisTrain& value : trains)
		text += value.runtimeId;
	return text;
}

// The order of the trains in the blocks they share (capacityTrainOrder), and the first pair of a list that goes against it.
bool trainOrder() {
	// X enters the blocks first, at block 1. Y comes later from its own branch block and reaches the shared blocks 3 and 4
	// before X, so it leads X. Z follows both on every block. W shares no block and goes by its entry time among the
	// trains that may come next.
	const auto x = entering("X", 0.0, {occupation("1", 0.0, 60.0), occupation("2", 100.0, 160.0), occupation("3", 200.0, 260.0), occupation("4", 300.0, 360.0)});
	const auto y = entering("Y", 50.0, {occupation("branch", 50.0, 110.0), occupation("3", 150.0, 210.0), occupation("4", 250.0, 310.0)});
	const auto z = entering("Z", 400.0, {occupation("1", 400.0, 460.0), occupation("4", 700.0, 760.0)});
	const auto w = entering("W", 100.0, {occupation("other", 100.0, 160.0)});
	bool ok = expect(idsOf(capacityTrainOrder({x, y, z, w})) == "YXWZ", "a merging train that reaches the shared blocks first leads");
	ok &= expect(idsOf(capacityTrainOrder({z, w, y, x})) == "YXWZ", "the run order does not change a decided order");
	ok &= expect(capacityOrderConflictFrom({y, x, w, z}).first.empty(), "the order of the shared blocks has no conflict");
	const auto conflict = capacityOrderConflictFrom({x, w, y, z});
	ok &= expect(conflict.first == "X" && conflict.second == "Y", "X listed before Y, which leads it, is the conflict");

	// A leads B on P, B leads C on Q and C leads A on R: the leaders form a cycle, so the earliest entry, A, comes first.
	const auto a = entering("A", 0.0, {occupation("P", 0.0, 10.0), occupation("R", 40.0, 50.0)});
	const auto b = entering("B", 10.0, {occupation("P", 10.0, 20.0), occupation("Q", 20.0, 30.0)});
	const auto c = entering("C", 5.0, {occupation("Q", 30.0, 40.0), occupation("R", 5.0, 15.0)});
	ok &= expect(idsOf(capacityTrainOrder({b, c, a})) == "ABC", "a cycle of leaders goes on from the earliest entry");
	const auto overtaken = capacityOrderConflictFrom({a, b, c});
	ok &= expect(overtaken.first == "A" && overtaken.second == "C", "an order of a cycle has a conflict");

	// B leads C, C leads E and E leads B, and all three lead D, which enters first. D waits for the cycle.
	const auto cb = entering("B", 10.0, {occupation("BC", 10.0, 20.0), occupation("EB", 50.0, 60.0), occupation("BD", 100.0, 110.0)});
	const auto cc = entering("C", 20.0, {occupation("BC", 30.0, 40.0), occupation("CE", 40.0, 50.0), occupation("CD", 100.0, 110.0)});
	const auto ce = entering("E", 30.0, {occupation("CE", 60.0, 70.0), occupation("EB", 30.0, 40.0), occupation("ED", 100.0, 110.0)});
	const auto cd = entering("D", 0.0, {occupation("BD", 200.0, 210.0), occupation("CD", 200.0, 210.0), occupation("ED", 200.0, 210.0)});
	ok &= expect(idsOf(capacityTrainOrder({cd, cb, cc, ce})) == "BCED", "a train that the trains of a cycle lead comes after the cycle");

	// Two trains that start together on their shared block: the earlier entry leads, then the run order.
	const auto early = entering("E", 3.0, {occupation("S", 10.0, 20.0)});
	const auto late = entering("L", 5.0, {occupation("S", 10.0, 20.0)});
	ok &= expect(idsOf(capacityTrainOrder({late, early})) == "EL", "on a tie the earlier entry leads");
	ok &= expect(capacityOrderConflictFrom({late, early}).first.empty(), "trains that start together on their shared block are no conflict");
	auto same = early;
	same.runtimeId = "F";
	ok &= expect(idsOf(capacityTrainOrder({same, early})) == "FE" && idsOf(capacityTrainOrder({early, same})) == "EF",
		"on a full tie the run order leads");
	ok &= expect(capacityOrderConflictFrom({same, early}).first.empty(), "a full tie is no conflict");
	ok &= expect(capacityTrainOrder({}).empty(), "no trains, no order");
	return ok;
}

// Trains that share no resource with the train before them, and the first train one period later.
bool partialChains() {
	// A and B share X. C shares nothing, so it starts with A after compression, not after B.
	const auto a = train("A", 10.0, {occupation("X", 0.0, 20.0)});
	const auto b = train("B", 50.0, {occupation("X", 5.0, 25.0)});
	const auto c = train("C", 500.0, {occupation("Z", 0.0, 10.0)});
	const auto chain = analyzeCapacity({a, b, c}, 100.0, "B");
	bool ok = expect(chain.analyzable && chain.compression[1].compressedReference == 25.0
			&& chain.compression[2].compressedReference == 10.0 && chain.compression[2].governingPredecessors.empty(),
		"a train that shares nothing with earlier trains starts with the first train");
	ok &= expect(chain.cycleTime == 15.0 && chain.cyclePercentage == 15.0, "the cycle to B");
	const auto unshared = analyzeCapacity({a, b, c}, 100.0, "C");
	ok &= expect(unshared.analyzable && unshared.cycleEndIdentity == "C" && unshared.cycleTime == -1.0
			&& unshared.cyclePercentage == -1.0,
		"a cycle-closing train that shares nothing with an earlier train closes no cycle");

	// The first train one period later shares every resource with the first train.
	const CapacityAnalysisTrain next = capacityNextPeriodTrain(a, 100.0);
	ok &= expect(next.runtimeId == "A (next period)" && capacityNextPeriodIdentity("A") == next.runtimeId && next.operatingCode == "A"
			&& next.referenceLabel == "A" && next.referenceSource == "test" && next.profileReferenceTime == 100.0
			&& next.scheduledReferenceTime == 110.0 && next.occupations.size() == 1 && next.occupations[0].startOccTime == 100.0
			&& next.occupations[0].endOccTime == 120.0 && next.occupations[0].endClearTime == 120.0
			&& next.occupations[0].startRunTime == -1.0,
		"the next-period train moves its references and occupation times by the period");
	CapacityAnalysisTrain unplanned = a;
	unplanned.scheduledReferenceTime = -1.0;
	ok &= expect(capacityNextPeriodTrain(unplanned, 100.0).scheduledReferenceTime == -1.0, "a missing planned reference stays missing");
	// A at 10, B at 10 + 15. The copy needs 20 s after A and 25 s after B, so it closes the cycle at 50 s.
	const auto repeated = analyzeCapacity({a, b, next}, 100.0, next.runtimeId);
	ok &= expect(repeated.analyzable && repeated.compression[2].compressedReference == 50.0 && repeated.cycleTime == 40.0
			&& repeated.cyclePercentage == 40.0 && repeated.cycleEndIdentity == "A (next period)",
		"the first train one period later closes the cycle");
	ok &= expect(repeated.compression[2].governingPredecessors.size() == 1
			&& repeated.compression[2].governingPredecessors[0].predecessorIdentity == "B",
		"the last train governs the next-period train");
	return ok;
}

// Trains that enter the analysed blocks at different blocks. P runs only over Z, from 0 s to 100 s. A runs over X, Y and
// Z, and reaches Z 200 s after its reference. A can enter 100 s before P, so that it reaches Z as P leaves it, and P one
// period later follows A on Z at 220 s.
bool differentEntries() {
	const auto p = entering("P", 0.0, {occupation("Z", 0.0, 100.0)});
	const auto a = entering("A", 0.0, {occupation("X", 0.0, 120.0), occupation("Y", 100.0, 220.0), occupation("Z", 200.0, 320.0)});
	const CapacityAnalysisTrain next = capacityNextPeriodTrain(p, 3600.0);
	const auto chain = analyzeCapacity({p, a, next}, 3600.0, next.runtimeId);
	bool ok = expect(chain.pairs.size() == 2 && chain.pairs[0].minimumHeadway == -100.0 && chain.pairs[0].buffer == 100.0,
		"the minimum headway of P and A is below 0");
	ok &= expect(chain.analyzable && chain.conflictFree && chain.compression[1].compressedReference == -100.0
			&& chain.compression[2].compressedReference == 220.0 && chain.cycleTime == 220.0,
		"A enters 100 s before P and P one period later follows it at 220 s");
	ok &= expect(chain.compressedOccupations[1].size() == 3 && chain.compressedOccupations[1][2].startOccTime == 100.0,
		"the occupations of A move with it");
	// W shares nothing with P or A, so it starts with P.
	const auto w = entering("W", 50.0, {occupation("other", 50.0, 60.0)});
	const auto withW = analyzeCapacity({p, a, w, next}, 3600.0, next.runtimeId);
	ok &= expect(withW.analyzable && withW.compression[2].compressedReference == 0.0 && withW.compression[2].governingPredecessors.empty()
			&& withW.cycleTime == 220.0,
		"a train that shares nothing with earlier trains still starts with the first train");
	return ok;
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
		"no shared resource gives no minimum headway");
	ok &= expect(noShared.analyzable && noShared.compression[1].compressedReference == 0.0,
		"two trains without a shared resource are analysed, the second starting with the first");
	CapacityAnalysisTrain malformed = follower;
	malformed.profileReferenceTime = -1.0;
	ok &= expect(!analyzeCapacity({leader, malformed}).analyzable,
		"missing profile reference is not analyzable");
	const auto zeroDuration = analyzeCapacity({leader,
		train("B", 20.0, {occupation("2-B0", 5.0, 5.0)})});
	ok &= expect(!zeroDuration.pairs[0].hasSharedConstraint,
		"zero-duration occupations are rejected consistently with the diagram");
	const auto nearTouch = analyzeCapacity({train("A", 0.0, {occupation("G", 0.0, 20.0), occupation("X", 0.0, 20.00000005)}),
											   train("B", 20.0, {occupation("G", 5.0, 25.0), occupation("X", 5.0, 25.0)})},
		100.0, "B");
	ok &= expect(nearTouch.conflictFree && nearTouch.criticalBlocks.size() == 2,
		"touch tolerance is not also classified as an overlap conflict");

	const std::vector<CapacityAnalysisTrain> sequence = {
		train("A", 0.0, {occupation("AB", 0.0, 20.0), occupation("AC", 0.0, 100.0)}),
		train("B", 100.0, {occupation("AB", 0.0, 10.0), occupation("BC", 0.0, 5.0)}),
		train("C", 30.0, {occupation("AC", 0.0, 50.0), occupation("BC", 0.0, 5.0), occupation("CD", 0.0, 10.0)}),
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
	ok &= expect(compressed.conflictFree && compressed.analyzable, "compressed occupations have no overlaps");
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
	auto compressedSegments = buildBlockingTimeDiagramSegments(compressed.compressedOccupations,
		compressed.trainIdentities);
	restoreCompressedOriginalTimes(compressedSegments, compressed.compression);
	ok &= expect(!compressedSegments.empty()
			&& compressedSegments.front().endClearTime == 20.0
			&& compressedSegments.front().startPositionKm == 0.0
			&& compressedSegments.front().endPositionKm == 0.1,
		"compressed adapter retains original clearance and directed spatial endpoints");
	const auto criticalSegment = std::find_if(compressedSegments.begin(), compressedSegments.end(),
		[](const BlockingTimeDiagramSegment& segment) {
			return segment.capacityCritical && segment.blockId == "AB";
		});
	ok &= expect(criticalSegment != compressedSegments.end()
			&& criticalSegment->style != BlockingTimeSegmentStyle::Critical,
		"touching capacity-critical styling stays distinct from red overlap conflict");
	ok &= expect(compressedSegments.size() > 2
			&& compressed.compression[1].scheduledReference == 100.0
			&& compressed.compression[1].originalReference == 0.0
			&& compressedSegments[2].trainName == "B"
			&& compressedSegments[2].startTime == 20.0
			&& compressedSegments[2].originalStartTime == 0.0
			&& compressedSegments[2].originalEndTime == 10.0,
		"compressed diagram adapter and export evidence undo profile rather than timetable shift");
	ok &= expect(compressed.cycleTime == 100.0 && compressed.cyclePercentage == 50.0,
		"explicit cycle endpoint and transparent period percentage are exact");
	ok &= expect(compressed.firstIdentity == "A" && compressed.cycleEndIdentity == "C"
			&& compressed.compression.back().identity == "D",
		"cycle endpoint is explicit and independent of the last selected train");
	const auto missingCycleEnd = analyzeCapacity(sequence, 200.0);
	ok &= expect(missingCycleEnd.cycleEndIdentity.empty() && missingCycleEnd.cycleTime < 0.0,
		"cycle consumption is unavailable without an explicit closing occurrence");

	ok &= trainOrder();
	ok &= partialChains();
	ok &= differentEntries();

	if (!ok)
		return 1;
	std::cout << "all CapacityAnalysis tests passed\n";
	return 0;
}
