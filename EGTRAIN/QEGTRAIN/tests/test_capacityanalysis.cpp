#include "diagrams/CapacityAnalysis.h"
#include "diagrams/CapacityScope.h"

#include <QTest>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace {

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

// A on 2-B0 from 0 to 20 s.
CapacityAnalysisTrain leader() {
	return train("A", 0.0, {occupation("2-B0", 0.0, 20.0)});
}

// B on 2-B0 from 5 to 25 s, planned 20 s after A.
CapacityAnalysisTrain follower() {
	return train("B", 20.0, {occupation("2-B0", 5.0, 25.0)});
}

// Four trains A to D, each sharing a block with an earlier one, analysed over 200 s with C closing the cycle.
std::vector<CapacityAnalysisTrain> sequence() {
	return {train("A", 0.0, {occupation("AB", 0.0, 20.0), occupation("AC", 0.0, 100.0)}),
		train("B", 100.0, {occupation("AB", 0.0, 10.0), occupation("BC", 0.0, 5.0)}),
		train("C", 30.0, {occupation("AC", 0.0, 50.0), occupation("BC", 0.0, 5.0), occupation("CD", 0.0, 10.0)}),
		train("D", 120.0, {occupation("CD", 0.0, 5.0)})};
}

} // namespace

class TestCapacityAnalysis : public QObject {
	Q_OBJECT

private slots:
	void measuresHeadwayOfPair();
	void keepsNegativeBuffer();
	void matchesCompositeBlocks();
	void analysesTrainsWithoutSharedResource();
	void rejectsMissingProfileReference();
	void rejectsZeroDurationOccupations();
	void doesNotCountTouchAsConflict();
	void compressesAgainstEveryEarlierTrain();
	void keepsCriticalTouches();
	void restoresOriginalTimesInDiagram();
	void closesCycleAtChosenTrain();
	void ordersTrainsByTheirSharedBlocks();
	void startsUnsharedTrainWithFirst();
	void compressesTrainsWithDifferentEntries();
};

void TestCapacityAnalysis::measuresHeadwayOfPair() {
	const auto pair = analyzeCapacity({leader(), follower()}, 3600.0);
	// one adjacent pair row
	QCOMPARE(pair.pairs.size(), std::size_t(1));
	QVERIFY2(pair.pairs[0].hasSharedConstraint, "shared pair is valid");
	// governing minimum headway uses offsets
	QCOMPARE_EQ(pair.pairs[0].minimumHeadway, 15.0);
	// scheduled buffer is not clamped
	QCOMPARE_EQ(pair.pairs[0].buffer, 5.0);
	// headway retains governing block evidence
	QCOMPARE(pair.pairs[0].governingEvidence.size(), std::size_t(1));
	QCOMPARE(pair.pairs[0].governingEvidence[0].leaderBlockId, "2-B0");
	QCOMPARE(pair.pairs[0].governingEvidence[0].followerBlockId, "2-B0");
	const auto tied = analyzeCapacity({train("A", 0.0,
										   {occupation("AB", 0.0, 20.0), occupation("AC", 0.0, 20.0)}),
		train("B", 20.0, {occupation("AB", 5.0, 25.0), occupation("AC", 5.0, 25.0)})});
	// tied governing resources are all retained
	QCOMPARE(tied.pairs[0].governingEvidence.size(), std::size_t(2));
}

void TestCapacityAnalysis::keepsNegativeBuffer() {
	const auto negativeBuffer = analyzeCapacity({leader(),
		train("B", 10.0, {occupation("2-B0", 5.0, 25.0)})});
	// negative scheduled buffer is retained
	QCOMPARE_EQ(negativeBuffer.pairs[0].buffer, -5.0);
}

void TestCapacityAnalysis::matchesCompositeBlocks() {
	QVERIFY2(shareBlockingTimeResource("@2-B0@-1.0/@3-B1@-2.0", "2-B0"),
		"composite block matches its exact component");
	QVERIFY2(!shareBlockingTimeResource("2-B0", "2-B4"),
		"distinct decorated block resources do not match");
}

void TestCapacityAnalysis::analysesTrainsWithoutSharedResource() {
	const auto noShared = analyzeCapacity({leader(),
		train("B", 20.0, {occupation("other", 5.0, 25.0)})});
	// no shared resource gives no minimum headway
	QVERIFY(!noShared.pairs[0].hasSharedConstraint);
	QCOMPARE_LT(noShared.pairs[0].minimumHeadway, 0.0);
	// two trains without a shared resource are analysed, the second starting with the first
	QVERIFY(noShared.analyzable);
	QCOMPARE_EQ(noShared.compression[1].compressedReference, 0.0);
}

void TestCapacityAnalysis::rejectsMissingProfileReference() {
	CapacityAnalysisTrain malformed = follower();
	malformed.profileReferenceTime = -1.0;
	QVERIFY2(!analyzeCapacity({leader(), malformed}).analyzable,
		"missing profile reference is not analyzable");
}

void TestCapacityAnalysis::rejectsZeroDurationOccupations() {
	const auto zeroDuration = analyzeCapacity({leader(),
		train("B", 20.0, {occupation("2-B0", 5.0, 5.0)})});
	QVERIFY2(!zeroDuration.pairs[0].hasSharedConstraint,
		"zero-duration occupations are rejected consistently with the diagram");
}

void TestCapacityAnalysis::doesNotCountTouchAsConflict() {
	const auto nearTouch = analyzeCapacity({train("A", 0.0, {occupation("G", 0.0, 20.0), occupation("X", 0.0, 20.00000005)}),
											   train("B", 20.0, {occupation("G", 5.0, 25.0), occupation("X", 5.0, 25.0)})},
		100.0, "B");
	// touch tolerance is not also classified as an overlap conflict
	QVERIFY(nearTouch.conflictFree);
	QCOMPARE(nearTouch.criticalBlocks.size(), std::size_t(2));
}

void TestCapacityAnalysis::compressesAgainstEveryEarlierTrain() {
	const std::vector<CapacityAnalysisTrain> trains = sequence();
	const auto before = trains;
	const auto compressed = analyzeCapacity(trains, 200.0, "C");
	// capacity analysis does not mutate inputs
	QCOMPARE_EQ(trains[0].occupations[0].startOccTime, before[0].occupations[0].startOccTime);
	QCOMPARE_EQ(trains[2].scheduledReferenceTime, before[2].scheduledReferenceTime);
	// compression considers every earlier train
	QCOMPARE(compressed.compression.size(), std::size_t(4));
	QCOMPARE_EQ(compressed.compression[1].compressedReference, 20.0);
	QCOMPARE_EQ(compressed.compression[2].compressedReference, 100.0);
	QCOMPARE_EQ(compressed.compression[3].compressedReference, 110.0);
	// nonadjacent predecessor governs compression
	QCOMPARE(compressed.compression[2].governingPredecessors.size(), std::size_t(1));
	QCOMPARE(compressed.compression[2].governingPredecessors[0].predecessorIdentity, "A");
	// compressed occupations have no overlaps
	QVERIFY(compressed.conflictFree);
	QVERIFY(compressed.analyzable);
}

void TestCapacityAnalysis::keepsCriticalTouches() {
	const auto compressed = analyzeCapacity(sequence(), 200.0, "C");
	const auto hasCritical = [&compressed](const char* leaderId, const char* followerId, const char* blockId) {
		return std::any_of(compressed.criticalBlocks.begin(), compressed.criticalBlocks.end(),
			[=](const CapacityCriticalBlock& critical) {
				return critical.leaderIdentity == leaderId && critical.followerIdentity == followerId
					&& critical.leaderBlockId == blockId && std::abs(critical.gap) < 1e-9;
			});
	};
	// adjacent and nonadjacent governing touches are retained separately
	QCOMPARE(compressed.criticalBlocks.size(), std::size_t(3));
	QVERIFY(hasCritical("A", "B", "AB"));
	QVERIFY(hasCritical("A", "C", "AC"));
	QVERIFY(hasCritical("C", "D", "CD"));
}

void TestCapacityAnalysis::restoresOriginalTimesInDiagram() {
	const auto compressed = analyzeCapacity(sequence(), 200.0, "C");
	auto compressedSegments = buildBlockingTimeDiagramSegments(compressed.compressedOccupations,
		compressed.trainIdentities);
	restoreCompressedOriginalTimes(compressedSegments, compressed.compression);
	// compressed adapter retains original clearance and directed spatial endpoints
	QVERIFY(!compressedSegments.empty());
	QCOMPARE_EQ(compressedSegments.front().endClearTime, 20.0);
	QCOMPARE_EQ(compressedSegments.front().startPositionKm, 0.0);
	QCOMPARE_EQ(compressedSegments.front().endPositionKm, 0.1);
	const auto criticalSegment = std::find_if(compressedSegments.begin(), compressedSegments.end(),
		[](const BlockingTimeDiagramSegment& segment) {
			return segment.capacityCritical && segment.blockId == "AB";
		});
	// touching capacity-critical styling stays distinct from red overlap conflict
	QVERIFY(criticalSegment != compressedSegments.end());
	QCOMPARE_NE(criticalSegment->style, BlockingTimeSegmentStyle::Critical);
	// compressed diagram adapter and export evidence undo profile rather than timetable shift
	QCOMPARE_GT(compressedSegments.size(), std::size_t(2));
	QCOMPARE_EQ(compressed.compression[1].scheduledReference, 100.0);
	QCOMPARE_EQ(compressed.compression[1].originalReference, 0.0);
	QCOMPARE(compressedSegments[2].trainName, "B");
	QCOMPARE_EQ(compressedSegments[2].startTime, 20.0);
	QCOMPARE_EQ(compressedSegments[2].originalStartTime, 0.0);
	QCOMPARE_EQ(compressedSegments[2].originalEndTime, 10.0);
}

void TestCapacityAnalysis::closesCycleAtChosenTrain() {
	const auto compressed = analyzeCapacity(sequence(), 200.0, "C");
	// explicit cycle endpoint and transparent period percentage are exact
	QCOMPARE_EQ(compressed.cycleTime, 100.0);
	QCOMPARE_EQ(compressed.cyclePercentage, 50.0);
	// cycle endpoint is explicit and independent of the last selected train
	QCOMPARE(compressed.firstIdentity, "A");
	QCOMPARE(compressed.cycleEndIdentity, "C");
	QCOMPARE(compressed.compression.back().identity, "D");
	const auto missingCycleEnd = analyzeCapacity(sequence(), 200.0);
	// cycle consumption is unavailable without an explicit closing occurrence
	QVERIFY(missingCycleEnd.cycleEndIdentity.empty());
	QCOMPARE_LT(missingCycleEnd.cycleTime, 0.0);
}

// The order of the trains in the blocks they share (capacityTrainOrder), and the first pair of a list that goes against it.
void TestCapacityAnalysis::ordersTrainsByTheirSharedBlocks() {
	// X enters the blocks first, at block 1. Y comes later from its own branch block and reaches the shared blocks 3 and 4
	// before X, so it leads X. Z follows both on every block. W shares no block and goes by its entry time among the
	// trains that may come next.
	const auto x = entering("X", 0.0, {occupation("1", 0.0, 60.0), occupation("2", 100.0, 160.0), occupation("3", 200.0, 260.0), occupation("4", 300.0, 360.0)});
	const auto y = entering("Y", 50.0, {occupation("branch", 50.0, 110.0), occupation("3", 150.0, 210.0), occupation("4", 250.0, 310.0)});
	const auto z = entering("Z", 400.0, {occupation("1", 400.0, 460.0), occupation("4", 700.0, 760.0)});
	const auto w = entering("W", 100.0, {occupation("other", 100.0, 160.0)});
	// a merging train that reaches the shared blocks first leads
	QCOMPARE(idsOf(capacityTrainOrder({x, y, z, w})), "YXWZ");
	// the run order does not change a decided order
	QCOMPARE(idsOf(capacityTrainOrder({z, w, y, x})), "YXWZ");
	QVERIFY2(capacityOrderConflictFrom({y, x, w, z}).first.empty(), "the order of the shared blocks has no conflict");
	const auto conflict = capacityOrderConflictFrom({x, w, y, z});
	// X listed before Y, which leads it, is the conflict
	QCOMPARE(conflict.first, "X");
	QCOMPARE(conflict.second, "Y");

	// A leads B on P, B leads C on Q and C leads A on R: the leaders form a cycle, so the earliest entry, A, comes first.
	const auto a = entering("A", 0.0, {occupation("P", 0.0, 10.0), occupation("R", 40.0, 50.0)});
	const auto b = entering("B", 10.0, {occupation("P", 10.0, 20.0), occupation("Q", 20.0, 30.0)});
	const auto c = entering("C", 5.0, {occupation("Q", 30.0, 40.0), occupation("R", 5.0, 15.0)});
	// a cycle of leaders goes on from the earliest entry
	QCOMPARE(idsOf(capacityTrainOrder({b, c, a})), "ABC");
	const auto overtaken = capacityOrderConflictFrom({a, b, c});
	// an order of a cycle has a conflict
	QCOMPARE(overtaken.first, "A");
	QCOMPARE(overtaken.second, "C");

	// B leads C, C leads E and E leads B, and all three lead D, which enters first. D waits for the cycle.
	const auto cb = entering("B", 10.0, {occupation("BC", 10.0, 20.0), occupation("EB", 50.0, 60.0), occupation("BD", 100.0, 110.0)});
	const auto cc = entering("C", 20.0, {occupation("BC", 30.0, 40.0), occupation("CE", 40.0, 50.0), occupation("CD", 100.0, 110.0)});
	const auto ce = entering("E", 30.0, {occupation("CE", 60.0, 70.0), occupation("EB", 30.0, 40.0), occupation("ED", 100.0, 110.0)});
	const auto cd = entering("D", 0.0, {occupation("BD", 200.0, 210.0), occupation("CD", 200.0, 210.0), occupation("ED", 200.0, 210.0)});
	// a train that the trains of a cycle lead comes after the cycle
	QCOMPARE(idsOf(capacityTrainOrder({cd, cb, cc, ce})), "BCED");

	// Two trains that start together on their shared block: the earlier entry leads, then the run order.
	const auto early = entering("E", 3.0, {occupation("S", 10.0, 20.0)});
	const auto late = entering("L", 5.0, {occupation("S", 10.0, 20.0)});
	// on a tie the earlier entry leads
	QCOMPARE(idsOf(capacityTrainOrder({late, early})), "EL");
	QVERIFY2(capacityOrderConflictFrom({late, early}).first.empty(), "trains that start together on their shared block are no conflict");
	auto same = early;
	same.runtimeId = "F";
	// on a full tie the run order leads
	QCOMPARE(idsOf(capacityTrainOrder({same, early})), "FE");
	QCOMPARE(idsOf(capacityTrainOrder({early, same})), "EF");
	QVERIFY2(capacityOrderConflictFrom({same, early}).first.empty(), "a full tie is no conflict");
	QVERIFY2(capacityTrainOrder({}).empty(), "no trains, no order");
}

// Trains that share no resource with the train before them, and the first train one period later.
void TestCapacityAnalysis::startsUnsharedTrainWithFirst() {
	// A and B share X. C shares nothing, so it starts with A after compression, not after B.
	const auto a = train("A", 10.0, {occupation("X", 0.0, 20.0)});
	const auto b = train("B", 50.0, {occupation("X", 5.0, 25.0)});
	const auto c = train("C", 500.0, {occupation("Z", 0.0, 10.0)});
	const auto chain = analyzeCapacity({a, b, c}, 100.0, "B");
	// a train that shares nothing with earlier trains starts with the first train
	QVERIFY(chain.analyzable);
	QCOMPARE_EQ(chain.compression[1].compressedReference, 25.0);
	QCOMPARE_EQ(chain.compression[2].compressedReference, 10.0);
	QVERIFY(chain.compression[2].governingPredecessors.empty());
	// the cycle to B
	QCOMPARE_EQ(chain.cycleTime, 15.0);
	QCOMPARE_EQ(chain.cyclePercentage, 15.0);
	const auto unshared = analyzeCapacity({a, b, c}, 100.0, "C");
	// a cycle-closing train that shares nothing with an earlier train closes no cycle
	QVERIFY(unshared.analyzable);
	QCOMPARE(unshared.cycleEndIdentity, "C");
	QCOMPARE_EQ(unshared.cycleTime, -1.0);
	QCOMPARE_EQ(unshared.cyclePercentage, -1.0);

	// The first train one period later shares every resource with the first train.
	const CapacityAnalysisTrain next = capacityNextPeriodTrain(a, 100.0);
	// the next-period train moves its references and occupation times by the period
	QCOMPARE(next.runtimeId, "A (next period)");
	QCOMPARE(capacityNextPeriodIdentity("A"), next.runtimeId);
	QCOMPARE(next.operatingCode, "A");
	QCOMPARE(next.referenceLabel, "A");
	QCOMPARE(next.referenceSource, "test");
	QCOMPARE_EQ(next.profileReferenceTime, 100.0);
	QCOMPARE_EQ(next.scheduledReferenceTime, 110.0);
	QCOMPARE(next.occupations.size(), std::size_t(1));
	QCOMPARE_EQ(next.occupations[0].startOccTime, 100.0);
	QCOMPARE_EQ(next.occupations[0].endOccTime, 120.0);
	QCOMPARE_EQ(next.occupations[0].endClearTime, 120.0);
	QCOMPARE_EQ(next.occupations[0].startRunTime, -1.0);
	CapacityAnalysisTrain unplanned = a;
	unplanned.scheduledReferenceTime = -1.0;
	// a missing planned reference stays missing
	QCOMPARE_EQ(capacityNextPeriodTrain(unplanned, 100.0).scheduledReferenceTime, -1.0);
	// A at 10, B at 10 + 15. The copy needs 20 s after A and 25 s after B, so it closes the cycle at 50 s.
	const auto repeated = analyzeCapacity({a, b, next}, 100.0, next.runtimeId);
	// the first train one period later closes the cycle
	QVERIFY(repeated.analyzable);
	QCOMPARE_EQ(repeated.compression[2].compressedReference, 50.0);
	QCOMPARE_EQ(repeated.cycleTime, 40.0);
	QCOMPARE_EQ(repeated.cyclePercentage, 40.0);
	QCOMPARE(repeated.cycleEndIdentity, "A (next period)");
	// the last train governs the next-period train
	QCOMPARE(repeated.compression[2].governingPredecessors.size(), std::size_t(1));
	QCOMPARE(repeated.compression[2].governingPredecessors[0].predecessorIdentity, "B");
}

// Trains that enter the analysed blocks at different blocks. P runs only over Z, from 0 s to 100 s. A runs over X, Y and
// Z, and reaches Z 200 s after its reference. A can enter 100 s before P, so that it reaches Z as P leaves it, and P one
// period later follows A on Z at 220 s.
void TestCapacityAnalysis::compressesTrainsWithDifferentEntries() {
	const auto p = entering("P", 0.0, {occupation("Z", 0.0, 100.0)});
	const auto a = entering("A", 0.0, {occupation("X", 0.0, 120.0), occupation("Y", 100.0, 220.0), occupation("Z", 200.0, 320.0)});
	const CapacityAnalysisTrain next = capacityNextPeriodTrain(p, 3600.0);
	const auto chain = analyzeCapacity({p, a, next}, 3600.0, next.runtimeId);
	// the minimum headway of P and A is below 0
	QCOMPARE(chain.pairs.size(), std::size_t(2));
	QCOMPARE_EQ(chain.pairs[0].minimumHeadway, -100.0);
	QCOMPARE_EQ(chain.pairs[0].buffer, 100.0);
	// A enters 100 s before P and P one period later follows it at 220 s
	QVERIFY(chain.analyzable);
	QVERIFY(chain.conflictFree);
	QCOMPARE_EQ(chain.compression[1].compressedReference, -100.0);
	QCOMPARE_EQ(chain.compression[2].compressedReference, 220.0);
	QCOMPARE_EQ(chain.cycleTime, 220.0);
	// the occupations of A move with it
	QCOMPARE(chain.compressedOccupations[1].size(), std::size_t(3));
	QCOMPARE_EQ(chain.compressedOccupations[1][2].startOccTime, 100.0);
	// W shares nothing with P or A, so it starts with P.
	const auto w = entering("W", 50.0, {occupation("other", 50.0, 60.0)});
	const auto withW = analyzeCapacity({p, a, w, next}, 3600.0, next.runtimeId);
	// a train that shares nothing with earlier trains still starts with the first train
	QVERIFY(withW.analyzable);
	QCOMPARE_EQ(withW.compression[2].compressedReference, 0.0);
	QVERIFY(withW.compression[2].governingPredecessors.empty());
	QCOMPARE_EQ(withW.cycleTime, 220.0);
}

QTEST_APPLESS_MAIN(TestCapacityAnalysis)
#include "test_capacityanalysis.moc"
