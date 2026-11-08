// Every value in this file is synthetic. The numbers of the first case were
// copied once from the made-up line of the committed test fixture; the test
// reads no file. Times are in seconds and percentages are in percent. Train
// ids and block ids are plain labels.

#include "diagrams/AreaCapacity.h"

#include <QStringList>
#include <QTest>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <string>
#include <vector>

namespace {

bool near(double a, double b) {
	return std::abs(a - b) < 1e-9;
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
	std::initializer_list<BlockingTimeDiagramInput> occupations, double profile = 0.0) {
	CapacityAnalysisTrain value;
	value.runtimeId = id;
	value.operatingCode = id;
	value.profileReferenceTime = profile;
	value.scheduledReferenceTime = scheduled;
	value.referenceLabel = id;
	value.referenceSource = "test";
	value.occupations = occupations;
	return value;
}

AreaCapacityTrain areaTrain(const CapacityAnalysisTrain& value, bool reversed = false) {
	AreaCapacityTrain result;
	result.train = value;
	result.reversedDirection = reversed;
	return result;
}

AreaCapacityInput areaInput(std::initializer_list<const char*> resources,
	std::initializer_list<AreaCapacityTrain> selected, double period, const char* cycleEnd = "") {
	AreaCapacityInput value;
	value.resourceIds.assign(resources.begin(), resources.end());
	value.selected.assign(selected.begin(), selected.end());
	value.periodSeconds = period;
	value.cycleEndIdentity = cycleEnd;
	return value;
}

// The row of one resource; a row that is not there has an empty resource id.
AreaCapacityResource rowFor(const AreaCapacityResult& result, const std::string& resourceId) {
	for (const AreaCapacityResource& row : result.resources) {
		if (row.resourceId == resourceId)
			return row;
	}
	return AreaCapacityResource();
}

// Exact match of the kind and of the three ids that are given (empty when not given).
bool hasNote(const AreaCapacityResult& result, AreaCapacityNoteKind kind, const std::string& trainId = "",
	const std::string& otherTrainId = "", const std::string& resourceId = "") {
	return std::any_of(result.notes.begin(), result.notes.end(), [&](const AreaCapacityNote& note) {
		return note.kind == kind && note.trainId == trainId && note.otherTrainId == otherTrainId
			&& note.resourceId == resourceId;
	});
}

int countNotes(const AreaCapacityResult& result, AreaCapacityNoteKind kind) {
	return static_cast<int>(std::count_if(result.notes.begin(), result.notes.end(),
		[kind](const AreaCapacityNote& note) { return note.kind == kind; }));
}

// Two trains on one block: A on [0, 20] and B on [5, 25], used by several cases.
AreaCapacityInput overlappingPair(double period) {
	const AreaCapacityTrain first = areaTrain(train("A", 0.0, {occupation("X", 0.0, 20.0)}));
	const AreaCapacityTrain second = areaTrain(train("B", 20.0, {occupation("X", 5.0, 25.0)}));
	return areaInput({"X"}, {first, second}, period, "B");
}


bool sameOccupation(const BlockingTimeDiagramInput& a, const BlockingTimeDiagramInput& b) {
	return a.blockId == b.blockId && a.startOccTime == b.startOccTime && a.endOccTime == b.endOccTime
		&& a.posStart == b.posStart && a.posEnd == b.posEnd && a.isComplete == b.isComplete
		&& a.capacityCritical == b.capacityCritical && a.endClearTime == b.endClearTime;
}

bool sameTrains(const std::vector<AreaCapacityTrain>& a, const std::vector<AreaCapacityTrain>& b) {
	if (a.size() != b.size())
		return false;
	for (std::size_t index = 0; index < a.size(); ++index) {
		const CapacityAnalysisTrain& first = a[index].train;
		const CapacityAnalysisTrain& second = b[index].train;
		if (first.runtimeId != second.runtimeId || a[index].reversedDirection != b[index].reversedDirection
			|| first.profileReferenceTime != second.profileReferenceTime
			|| first.scheduledReferenceTime != second.scheduledReferenceTime
			|| first.occupations.size() != second.occupations.size())
			return false;
		for (std::size_t occupationIndex = 0; occupationIndex < first.occupations.size(); ++occupationIndex) {
			if (!sameOccupation(first.occupations[occupationIndex], second.occupations[occupationIndex]))
				return false;
		}
	}
	return true;
}

bool sameResult(const AreaCapacityResult& a, const AreaCapacityResult& b) {
	if (a.periodSeconds != b.periodSeconds || a.envelopeSeconds != b.envelopeSeconds
		|| a.envelopePercentage != b.envelopePercentage || a.complete != b.complete
		|| a.resources.size() != b.resources.size() || a.notes.size() != b.notes.size()
		|| a.chain.cycleTime != b.chain.cycleTime || a.chain.cyclePercentage != b.chain.cyclePercentage
		|| a.chain.trainIdentities != b.chain.trainIdentities
		|| a.chain.compression.size() != b.chain.compression.size())
		return false;
	for (std::size_t index = 0; index < a.resources.size(); ++index) {
		const AreaCapacityResource& first = a.resources[index];
		const AreaCapacityResource& second = b.resources[index];
		if (first.resourceId != second.resourceId || first.trainCount != second.trainCount
			|| first.occupationCount != second.occupationCount || first.occupationSeconds != second.occupationSeconds
			|| first.unionSeconds != second.unionSeconds || first.occupationPercentage != second.occupationPercentage
			|| first.governing != second.governing)
			return false;
	}
	for (std::size_t index = 0; index < a.notes.size(); ++index) {
		if (a.notes[index].kind != b.notes[index].kind || a.notes[index].trainId != b.notes[index].trainId
			|| a.notes[index].otherTrainId != b.notes[index].otherTrainId
			|| a.notes[index].resourceId != b.notes[index].resourceId)
			return false;
	}
	for (std::size_t index = 0; index < a.chain.compression.size(); ++index) {
		if (a.chain.compression[index].compressedReference != b.chain.compression[index].compressedReference)
			return false;
	}
	return true;
}

} // namespace

class TestAreaCapacity : public QObject {
	Q_OBJECT

private slots:
	void analysesLineFixtureOfTwoTrains();
	void sumsAndUnitesOccupations();
	void analysesSequenceOfFourTrains();
	void countsRepeatedResource();
	void leavesOpposingTrainOutOfChain();
	void reportsEmptyScope_data();
	void reportsEmptyScope();
	void keepsOneRowPerResourceId();
	void reportsInvalidPeriod_data();
	void reportsInvalidPeriod();
	void reportsFewTrains();
	void leavesUnusableOccupationOut_data();
	void leavesUnusableOccupationOut();
	void notesUnusableOccupationsOnce();
	void reportsUnselectedTraffic();
	void reportsPairWithoutSharedResource();
	void reportsUnusableReference_data();
	void reportsUnusableReference();
	void reportsUnusableReferenceOfLeader();
	void reportsDuplicateIdentity();
	void marksGoverningRows();
	void ignoresOccupationsOutsideArea();
	void leavesInputUnchanged();
};

void TestAreaCapacity::analysesLineFixtureOfTwoTrains() {
	// Eight blocks, F1 then F2. The profile reference is the start of the first
	// block (59 and 199), the scheduled reference the authored departure (120 and 180).
	const CapacityAnalysisTrain first = train("F1", 120.0,
		{occupation("@0-B0@", 59.0, 146.0), occupation("@1-B0@", 53.5, 201.0), occupation("@2-B0@", 135.5, 257.0),
			occupation("@3-B0@", 190.5, 674.0), occupation("@4-B0@", 246.5, 748.0), occupation("@5-B0@", 654.5, 803.0),
			occupation("@6-B0@", 737.5, 859.0), occupation("@7-B0@", 792.5, 983.0)},
		59.0);
	const CapacityAnalysisTrain second = train("F2", 180.0,
		{occupation("@0-B0@", 199.0, 286.0), occupation("@1-B0@", 193.5, 357.0), occupation("@2-B0@", 275.5, 686.0),
			occupation("@3-B0@", 342.5, 892.0), occupation("@4-B0@", 666.5, 966.0), occupation("@5-B0@", 872.5, 1021.0),
			occupation("@6-B0@", 955.5, 1077.0), occupation("@7-B0@", 1010.5, 1201.0)},
		199.0);
	const AreaCapacityInput request = areaInput({"@0-B0@", "@1-B0@", "@2-B0@", "@3-B0@", "@4-B0@", "@5-B0@", "@6-B0@", "@7-B0@"},
		{areaTrain(first), areaTrain(second)}, 3600.0, "F2");
	const AreaCapacityResult result = analyzeAreaCapacity(request);

	// Candidate headway per block: (F1 end - 59) - (F2 start - 199):
	// @0 87 - 0 = 87, @1 142 - (-5.5) = 147.5, @2 198 - 76.5 = 121.5, @3 615 - 143.5 = 471.5,
	// @4 689 - 467.5 = 221.5, @5 744 - 673.5 = 70.5, @6 800 - 756.5 = 43.5, @7 924 - 811.5 = 112.5.
	// line fixture, two trains: the minimum headway is the largest candidate, 471.5
	QCOMPARE(result.chain.pairs.size(), std::size_t(1));
	QVERIFY(near(result.chain.pairs[0].minimumHeadway, 471.5));
	// line fixture, two trains: the fourth block governs the pair
	QCOMPARE(result.chain.pairs.size(), std::size_t(1));
	QCOMPARE(result.chain.pairs[0].governingEvidence.size(), std::size_t(1));
	QCOMPARE(result.chain.pairs[0].governingEvidence[0].leaderBlockId, "@3-B0@");
	QVERIFY(near(result.chain.pairs[0].governingEvidence[0].candidateHeadway, 471.5));
	// Scheduled headway 180 - 120 = 60 and buffer 60 - 471.5 = -411.5.
	// line fixture, two trains: scheduled headway and buffer
	QCOMPARE(result.chain.pairs.size(), std::size_t(1));
	QVERIFY(near(result.chain.pairs[0].scheduledHeadway, 60.0));
	QVERIFY(near(result.chain.pairs[0].buffer, -411.5));
	// F2 is compressed to 120 + 471.5 = 591.5.
	// line fixture, two trains: compressed reference of the second train
	QCOMPARE(result.chain.compression.size(), std::size_t(2));
	QVERIFY(near(result.chain.compression[1].compressedReference, 591.5));
	// Cycle 591.5 - 120 = 471.5 and 471.5 / 3600 * 100 = 13.0972... percent.
	// line fixture, two trains: cycle time and cycle percentage
	QVERIFY(near(result.chain.cycleTime, 471.5));
	QVERIFY(near(result.chain.cyclePercentage, 471.5 / 3600.0 * 100.0));
	QVERIFY(near(result.chain.cyclePercentage, 13.0972222222));
	// line fixture, two trains: the chain is analyzable
	QVERIFY(result.chain.conflictFree);
	QVERIFY(result.chain.analyzable);
	// After compression F1 ends on the fourth block at 674 + (120 - 59) = 735 and F2 starts at
	// 342.5 + (591.5 - 199) = 735: the one touch.
	// line fixture, two trains: one critical touch at 735
	QCOMPARE(result.chain.criticalBlocks.size(), std::size_t(1));
	QCOMPARE(result.chain.criticalBlocks[0].leaderBlockId, "@3-B0@");
	QCOMPARE(result.chain.criticalBlocks[0].followerBlockId, "@3-B0@");
	QVERIFY(near(result.chain.compressedOccupations[0][3].endOccTime, 735.0));
	QVERIFY(near(result.chain.compressedOccupations[1][3].startOccTime, 735.0));

	// Per block, F1 then F2 (end - start), the sum, and the union:
	// @0 87 + 87 = 174, disjoint, union 174.
	// @1 147.5 + 163.5 = 311, overlap, union 53.5..357 = 303.5.
	// @2 121.5 + 410.5 = 532, disjoint, union 532.
	// @3 483.5 + 549.5 = 1033, overlap, union 190.5..892 = 701.5.
	// @4 501.5 + 299.5 = 801, overlap, union 246.5..966 = 719.5.
	// @5 148.5 + 148.5 = 297, disjoint, union 297.
	// @6 121.5 + 121.5 = 243, disjoint, union 243.
	// @7 190.5 + 190.5 = 381, disjoint, union 381.
	const std::vector<double> sums = {174.0, 311.0, 532.0, 1033.0, 801.0, 297.0, 243.0, 381.0};
	const std::vector<double> unions = {174.0, 303.5, 532.0, 701.5, 719.5, 297.0, 243.0, 381.0};
	// line fixture, two trains: one row per block
	QCOMPARE(result.resources.size(), std::size_t(8));
	for (std::size_t row = 0; row < result.resources.size() && row < sums.size(); ++row) {
		const AreaCapacityResource& resource = result.resources[row];
		// line fixture, two trains: both trains occupy every block once
		QCOMPARE(resource.trainCount, 2);
		QCOMPARE(resource.occupationCount, 2);
		// line fixture, two trains: sum and union per block
		QVERIFY(near(resource.occupationSeconds, sums[row]));
		QVERIFY(near(resource.unionSeconds, unions[row]));
		QVERIFY2(near(resource.occupationPercentage, sums[row] / 3600.0 * 100.0),
			"line fixture, two trains: occupation percentage is the sum over the period");
		// line fixture, two trains: only the fourth block governs
		QCOMPARE(resource.governing, row == 3);
	}
	// 1033 / 3600 * 100 = 28.6944... percent and 801 / 3600 * 100 = 22.25 percent.
	// line fixture, two trains: percentages of the fourth and fifth block
	QCOMPARE(result.resources.size(), std::size_t(8));
	QVERIFY(near(result.resources[3].occupationPercentage, 28.6944444444));
	QVERIFY(near(result.resources[4].occupationPercentage, 22.25));
	// Envelope 1201 - 53.5 = 1147.5 and 1147.5 / 3600 * 100 = 31.875 percent.
	// line fixture, two trains: envelope of the removed area calculation
	QVERIFY(near(result.envelopeSeconds, 1147.5));
	QVERIFY(near(result.envelopePercentage, 31.875));
	QVERIFY2(near(result.periodSeconds, 3600.0), "line fixture, two trains: the period is kept");
	// line fixture, two trains: complete traffic and no note
	QVERIFY(result.complete);
	QVERIFY(result.notes.empty());
}

void TestAreaCapacity::sumsAndUnitesOccupations() {
	const AreaCapacityResult result = analyzeAreaCapacity(overlappingPair(100.0));
	const AreaCapacityResource row = rowFor(result, "X");
	// Sum (20 - 0) + (25 - 5) = 40; union 0..25 = 25; 40 / 100 * 100 = 40 percent.
	// sum and union: counts
	QCOMPARE(row.trainCount, 2);
	QCOMPARE(row.occupationCount, 2);
	// sum and union: seconds
	QVERIFY(near(row.occupationSeconds, 40.0));
	QVERIFY(near(row.unionSeconds, 25.0));
	QVERIFY2(near(row.occupationPercentage, 40.0), "sum and union: the percentage uses the sum");
	// Envelope 25 - 0 = 25 s and 25 percent.
	// sum and union: envelope
	QVERIFY(near(result.envelopeSeconds, 25.0));
	QVERIFY(near(result.envelopePercentage, 25.0));
	// Headway (20 - 0) - (5 - 0) = 15; B moves to 0 + 15, so the cycle is 15 s.
	// sum and union: the chain is the one of the capacity analysis
	QCOMPARE(result.chain.pairs.size(), std::size_t(1));
	QVERIFY(near(result.chain.pairs[0].minimumHeadway, 15.0));
	QVERIFY(near(result.chain.cycleTime, 15.0));
	// sum and union: complete and no note
	QVERIFY(result.complete);
	QVERIFY(result.notes.empty());
}

void TestAreaCapacity::analysesSequenceOfFourTrains() {
	const std::vector<CapacityAnalysisTrain> trains = {
		train("A", 0.0, {occupation("AB", 0.0, 20.0), occupation("AC", 0.0, 100.0)}),
		train("B", 100.0, {occupation("AB", 0.0, 10.0), occupation("BC", 0.0, 5.0)}),
		train("C", 30.0, {occupation("AC", 0.0, 50.0), occupation("BC", 0.0, 5.0), occupation("CD", 0.0, 10.0)}),
		train("D", 120.0, {occupation("CD", 0.0, 5.0)})};
	AreaCapacityInput request;
	request.resourceIds = {"AB", "AC", "BC", "CD"};
	for (const CapacityAnalysisTrain& value : trains)
		request.selected.push_back(areaTrain(value));
	request.periodSeconds = 200.0;
	request.cycleEndIdentity = "C";
	const AreaCapacityResult result = analyzeAreaCapacity(request);

	// Sums: AB 20 + 10 = 30, AC 100 + 50 = 150, BC 5 + 5 = 10, CD 10 + 5 = 15.
	// Unions: 20, 100, 5, 10. Percentages of 200: 15, 75, 5, 7.5.
	const std::vector<double> sums = {30.0, 150.0, 10.0, 15.0};
	const std::vector<double> unions = {20.0, 100.0, 5.0, 10.0};
	const std::vector<double> percentages = {15.0, 75.0, 5.0, 7.5};
	// four trains: one row per block
	QCOMPARE(result.resources.size(), std::size_t(4));
	for (std::size_t row = 0; row < result.resources.size() && row < sums.size(); ++row) {
		const AreaCapacityResource& resource = result.resources[row];
		// four trains: two trains per block
		QCOMPARE(resource.trainCount, 2);
		QCOMPARE(resource.occupationCount, 2);
		// four trains: sum, union and percentage per block
		QVERIFY(near(resource.occupationSeconds, sums[row]));
		QVERIFY(near(resource.unionSeconds, unions[row]));
		QVERIFY(near(resource.occupationPercentage, percentages[row]));
		// four trains: only AC governs
		QCOMPARE(resource.governing, row == 1);
	}
	// Envelope 100 - 0 = 100 s and 100 / 200 * 100 = 50 percent.
	// four trains: envelope
	QVERIFY(near(result.envelopeSeconds, 100.0));
	QVERIFY(near(result.envelopePercentage, 50.0));
	// Compressed references 0, 20, 100, 110; cycle 100 - 0 = 100 s and 50 percent.
	// four trains: compressed references
	QCOMPARE(result.chain.compression.size(), std::size_t(4));
	QVERIFY(near(result.chain.compression[0].compressedReference, 0.0));
	QVERIFY(near(result.chain.compression[1].compressedReference, 20.0));
	QVERIFY(near(result.chain.compression[2].compressedReference, 100.0));
	QVERIFY(near(result.chain.compression[3].compressedReference, 110.0));
	// four trains: cycle time and percentage
	QVERIFY(near(result.chain.cycleTime, 100.0));
	QVERIFY(near(result.chain.cyclePercentage, 50.0));

	const CapacityAnalysisResult direct = analyzeCapacity(trains, 200.0, "C");
	bool sameReferences = direct.compression.size() == result.chain.compression.size();
	for (std::size_t index = 0; sameReferences && index < direct.compression.size(); ++index)
		sameReferences = direct.compression[index].compressedReference == result.chain.compression[index].compressedReference;
	// four trains: the chain equals a direct chain analysis of the same trains
	QVERIFY(sameReferences);
	QCOMPARE_EQ(direct.cycleTime, result.chain.cycleTime);
	QCOMPARE_EQ(direct.cyclePercentage, result.chain.cyclePercentage);
	QCOMPARE(direct.criticalBlocks.size(), result.chain.criticalBlocks.size());
	// four trains: complete and no note
	QVERIFY(result.complete);
	QVERIFY(result.notes.empty());
}

void TestAreaCapacity::countsRepeatedResource() {
	// A is on X twice, B once.
	const AreaCapacityTrain repeating = areaTrain(train("A", 0.0, {occupation("X", 0.0, 10.0), occupation("X", 30.0, 40.0)}));
	const AreaCapacityTrain single = areaTrain(train("B", 50.0, {occupation("X", 50.0, 60.0)}));
	const AreaCapacityResult result = analyzeAreaCapacity(areaInput({"X"}, {repeating, single}, 100.0));
	const AreaCapacityResource row = rowFor(result, "X");
	// 10 + 10 + 10 = 30 s; the intervals are disjoint, so the union is 30 s as well.
	// repeated block: every occupation is counted
	QCOMPARE(row.occupationCount, 3);
	QCOMPARE(row.trainCount, 2);
	// repeated block: sum and union
	QVERIFY(near(row.occupationSeconds, 30.0));
	QVERIFY(near(row.unionSeconds, 30.0));
	// repeated block: one note for the train that repeats it
	QCOMPARE(countNotes(result, AreaCapacityNoteKind::RepeatedResource), 1);
	QVERIFY(hasNote(result, AreaCapacityNoteKind::RepeatedResource, "A", "", "X"));
	QCOMPARE(result.notes.size(), std::size_t(1));

	// One train with overlapping repeats: 10 + 10 = 20 s, union 0..15 = 15 s.
	const AreaCapacityResult overlapping = analyzeAreaCapacity(
		areaInput({"X"}, {areaTrain(train("A2", 0.0, {occupation("X", 0.0, 10.0), occupation("X", 5.0, 15.0)}))}, 100.0));
	const AreaCapacityResource overlappingRow = rowFor(overlapping, "X");
	// repeated block: overlapping repeats count once in the union
	QCOMPARE(overlappingRow.occupationCount, 2);
	QVERIFY(near(overlappingRow.occupationSeconds, 20.0));
	QVERIFY(near(overlappingRow.unionSeconds, 15.0));
	QVERIFY2(hasNote(overlapping, AreaCapacityNoteKind::RepeatedResource, "A2", "", "X"),
		"repeated block: overlapping repeats get the note");
}

void TestAreaCapacity::leavesOpposingTrainOutOfChain() {
	const AreaCapacityTrain forward = areaTrain(train("A", 0.0, {occupation("X", 0.0, 10.0), occupation("Y", 10.0, 20.0)}), false);
	const AreaCapacityTrain backward = areaTrain(train("B", 100.0, {occupation("X", 100.0, 110.0), occupation("Y", 90.0, 100.0)}), true);

	const AreaCapacityResult pair = analyzeAreaCapacity(areaInput({"X", "Y"}, {forward, backward}, 200.0));
	// opposing train: the chain keeps the first train
	QCOMPARE(pair.chain.trainIdentities.size(), std::size_t(1));
	QCOMPARE(pair.chain.trainIdentities[0], "A");
	// opposing train: one note for the train that is left out
	QCOMPARE(countNotes(pair, AreaCapacityNoteKind::OpposingTrain), 1);
	QVERIFY(hasNote(pair, AreaCapacityNoteKind::OpposingTrain, "B"));
	// opposing train: the chain has one train
	QCOMPARE(countNotes(pair, AreaCapacityNoteKind::FewerThanTwoTrains), 1);
	QCOMPARE(pair.notes.size(), std::size_t(2));
	// Both trains are counted in the rows: 10 + 10 = 20 s on X and on Y.
	// opposing train: the rows count both directions
	QCOMPARE(rowFor(pair, "X").trainCount, 2);
	QVERIFY(near(rowFor(pair, "X").occupationSeconds, 20.0));
	QCOMPARE(rowFor(pair, "Y").trainCount, 2);
	QVERIFY(near(rowFor(pair, "Y").occupationSeconds, 20.0));

	// A third train in the direction of A, with the opposing train between them in the order.
	const AreaCapacityTrain third = areaTrain(train("C", 40.0, {occupation("X", 40.0, 50.0)}), false);
	const AreaCapacityResult three = analyzeAreaCapacity(areaInput({"X", "Y"}, {forward, backward, third}, 200.0));
	// opposing train: the chain skips the train in the other direction
	QCOMPARE(three.chain.trainIdentities.size(), std::size_t(2));
	QCOMPARE(three.chain.trainIdentities[0], "A");
	QCOMPARE(three.chain.trainIdentities[1], "C");
	// opposing train: the only note is for the opposing train
	QCOMPARE(countNotes(three, AreaCapacityNoteKind::OpposingTrain), 1);
	QVERIFY(hasNote(three, AreaCapacityNoteKind::OpposingTrain, "B"));
	QCOMPARE(three.notes.size(), std::size_t(1));
	// opposing train: the rows count three trains
	QCOMPARE(rowFor(three, "X").trainCount, 3);
	QVERIFY(near(rowFor(three, "X").occupationSeconds, 30.0));
	QCOMPARE(rowFor(three, "Y").trainCount, 2);

	// The first selected train sets the direction.
	const AreaCapacityResult reversedFirst = analyzeAreaCapacity(areaInput({"X", "Y"}, {backward, forward}, 200.0));
	// opposing train: the direction of the first selected train decides
	QCOMPARE(reversedFirst.chain.trainIdentities.size(), std::size_t(1));
	QCOMPARE(reversedFirst.chain.trainIdentities[0], "B");
	QVERIFY(hasNote(reversedFirst, AreaCapacityNoteKind::OpposingTrain, "A"));

	// The cycle-closing occurrence is not in the chain.
	const AreaCapacityResult closing = analyzeAreaCapacity(areaInput({"X", "Y"}, {forward, backward}, 200.0, "B"));
	// opposing train: a closing occurrence that was left out gives no cycle
	QVERIFY(closing.chain.cycleEndIdentity.empty());
	QCOMPARE_LT(closing.chain.cycleTime, 0.0);
}

void TestAreaCapacity::reportsEmptyScope_data() {
	QTest::addColumn<QStringList>("scope");
	QTest::newRow("no id") << QStringList();
	QTest::newRow("empty and None") << QStringList({"", "None"});
}

// The two trains of overlappingPair with the given resource ids, once with a
// valid and once with an invalid period.
void TestAreaCapacity::reportsEmptyScope() {
	QFETCH(QStringList, scope);
	AreaCapacityInput request = overlappingPair(100.0);
	request.resourceIds.clear();
	for (const QString& id : scope)
		request.resourceIds.push_back(id.toStdString());
	const AreaCapacityResult result = analyzeAreaCapacity(request);
	// empty scope: the only note is EmptyScope
	QCOMPARE(result.notes.size(), std::size_t(1));
	QVERIFY(hasNote(result, AreaCapacityNoteKind::EmptyScope));
	// empty scope: no rows and no chain
	QVERIFY(result.resources.empty());
	QVERIFY(result.chain.trainIdentities.empty());
	// empty scope: no envelope and not complete
	QCOMPARE_LT(result.envelopeSeconds, 0.0);
	QCOMPARE_LT(result.envelopePercentage, 0.0);
	QVERIFY(!result.complete);

	request.periodSeconds = 0.0;
	const AreaCapacityResult noPeriod = analyzeAreaCapacity(request);
	// empty scope: an invalid period adds only its own note
	QCOMPARE(noPeriod.notes.size(), std::size_t(2));
	QVERIFY(hasNote(noPeriod, AreaCapacityNoteKind::EmptyScope));
	QVERIFY(hasNote(noPeriod, AreaCapacityNoteKind::InvalidPeriod));
}

void TestAreaCapacity::keepsOneRowPerResourceId() {
	AreaCapacityInput repeated = overlappingPair(100.0);
	repeated.resourceIds = {"X", "Y", "X", ""};
	const AreaCapacityResult result = analyzeAreaCapacity(repeated);
	// empty scope: repeated and empty ids give one row each in list order
	QCOMPARE(result.resources.size(), std::size_t(2));
	QCOMPARE(result.resources[0].resourceId, "X");
	QCOMPARE(result.resources[1].resourceId, "Y");
}

void TestAreaCapacity::reportsInvalidPeriod_data() {
	QTest::addColumn<double>("period");
	QTest::newRow("zero") << 0.0;
	QTest::newRow("negative") << -5.0;
	QTest::newRow("not a number") << std::numeric_limits<double>::quiet_NaN();
	QTest::newRow("infinite") << std::numeric_limits<double>::infinity();
}

void TestAreaCapacity::reportsInvalidPeriod() {
	QFETCH(double, period);
	const AreaCapacityResult result = analyzeAreaCapacity(overlappingPair(period));
	// invalid period: the only note is InvalidPeriod
	QCOMPARE(result.notes.size(), std::size_t(1));
	QVERIFY(hasNote(result, AreaCapacityNoteKind::InvalidPeriod));
	// invalid period: the period is unavailable
	QVERIFY(near(result.periodSeconds, -1.0));
	QVERIFY(near(result.chain.periodSeconds, -1.0));
	const AreaCapacityResource row = rowFor(result, "X");
	// invalid period: every percentage is unavailable
	QVERIFY(near(row.occupationPercentage, -1.0));
	QVERIFY(near(result.envelopePercentage, -1.0));
	QVERIFY(near(result.chain.cyclePercentage, -1.0));
	// The seconds are the ones of the valid period 100.
	// invalid period: seconds, governing row and cycle time are as for a valid period
	QVERIFY(near(row.occupationSeconds, 40.0));
	QVERIFY(near(row.unionSeconds, 25.0));
	QVERIFY(row.governing);
	QVERIFY(near(result.envelopeSeconds, 25.0));
	QVERIFY(near(result.chain.cycleTime, 15.0));
}

void TestAreaCapacity::reportsFewTrains() {
	const AreaCapacityResult single = analyzeAreaCapacity(
		areaInput({"X"}, {areaTrain(train("A", 0.0, {occupation("X", 0.0, 20.0)}))}, 100.0));
	// one train: the only note is FewerThanTwoTrains
	QVERIFY(hasNote(single, AreaCapacityNoteKind::FewerThanTwoTrains));
	QCOMPARE(single.notes.size(), std::size_t(1));
	// one train: the row is filled and the chain is not analyzable
	QCOMPARE(rowFor(single, "X").trainCount, 1);
	QVERIFY(near(rowFor(single, "X").occupationSeconds, 20.0));
	QVERIFY(!single.chain.analyzable);
	QVERIFY2(single.complete, "one train: the note does not change completeness");

	const AreaCapacityResult none = analyzeAreaCapacity(areaInput({"X", "Y"}, {}, 100.0));
	// no train: the only note is NoSelectedTraffic
	QCOMPARE(none.notes.size(), std::size_t(1));
	QVERIFY(hasNote(none, AreaCapacityNoteKind::NoSelectedTraffic));
	QCOMPARE(countNotes(none, AreaCapacityNoteKind::FewerThanTwoTrains), 0);
	// no train: the rows stay
	QCOMPARE(none.resources.size(), std::size_t(2));
	for (const AreaCapacityResource& row : none.resources) {
		// no train: every row is zero and none governs
		QCOMPARE(row.trainCount, 0);
		QCOMPARE(row.occupationCount, 0);
		QVERIFY(near(row.occupationSeconds, 0.0));
		QVERIFY(near(row.unionSeconds, 0.0));
		QVERIFY(near(row.occupationPercentage, 0.0));
		QVERIFY(!row.governing);
	}
	// no train: no envelope and not complete
	QVERIFY(near(none.envelopeSeconds, -1.0));
	QVERIFY(near(none.envelopePercentage, -1.0));
	QVERIFY(!none.complete);
}

void TestAreaCapacity::leavesUnusableOccupationOut_data() {
	QTest::addColumn<double>("start");
	QTest::addColumn<double>("end");
	QTest::addColumn<bool>("complete");
	QTest::newRow("incomplete") << 30.0 << 40.0 << false;
	QTest::newRow("no length") << 30.0 << 30.0 << true;
	QTest::newRow("start not a number") << std::numeric_limits<double>::quiet_NaN() << 40.0 << true;
	QTest::newRow("negative start") << -5.0 << 40.0 << true;
}

void TestAreaCapacity::leavesUnusableOccupationOut() {
	QFETCH(double, start);
	QFETCH(double, end);
	QFETCH(bool, complete);
	BlockingTimeDiagramInput bad = occupation("X", start, end);
	bad.isComplete = complete;
	CapacityAnalysisTrain value = train("A", 0.0, {occupation("X", 0.0, 20.0)});
	value.occupations.push_back(bad);
	const AreaCapacityResult result = analyzeAreaCapacity(areaInput({"X"}, {areaTrain(value)}, 100.0));
	QVERIFY2(hasNote(result, AreaCapacityNoteKind::IncompleteOccupation, "A", "", "X"),
		"unusable occupation: the note names the train and the resource");
	// unusable occupation: it is in no figure
	QCOMPARE(rowFor(result, "X").occupationCount, 1);
	QVERIFY(near(rowFor(result, "X").occupationSeconds, 20.0));
	QVERIFY(near(result.envelopeSeconds, 20.0));
	QVERIFY2(!result.complete, "unusable occupation: the traffic is not complete");
}

void TestAreaCapacity::notesUnusableOccupationsOnce() {
	BlockingTimeDiagramInput incomplete = occupation("X", 30.0, 40.0);
	incomplete.isComplete = false;

	// One note per train and resource, and one for each resource that is touched.
	CapacityAnalysisTrain repeated = train("A", 0.0, {occupation("X", 0.0, 20.0)});
	repeated.occupations.push_back(incomplete);
	repeated.occupations.push_back(incomplete);
	BlockingTimeDiagramInput incompleteY = occupation("Y", 0.0, 10.0);
	incompleteY.isComplete = false;
	repeated.occupations.push_back(incompleteY);
	const AreaCapacityResult twice = analyzeAreaCapacity(areaInput({"X", "Y"}, {areaTrain(repeated)}, 100.0));
	// unusable occupation: one note per train and resource
	QCOMPARE(countNotes(twice, AreaCapacityNoteKind::IncompleteOccupation), 2);
	QVERIFY(hasNote(twice, AreaCapacityNoteKind::IncompleteOccupation, "A", "", "X"));
	QVERIFY(hasNote(twice, AreaCapacityNoteKind::IncompleteOccupation, "A", "", "Y"));

	// A composite id that is unusable is reported for the first resource in list order.
	BlockingTimeDiagramInput composite = occupation("@2-B0@-1.0/@3-B1@-2.0", 0.0, 10.0);
	composite.isComplete = false;
	CapacityAnalysisTrain onComposite = train("A", 0.0, {});
	onComposite.occupations.push_back(composite);
	const AreaCapacityResult compositeResult = analyzeAreaCapacity(areaInput({"3-B1", "2-B0"}, {areaTrain(onComposite)}, 100.0));
	// unusable occupation: a composite id is reported for the first resource it touches
	QCOMPARE(countNotes(compositeResult, AreaCapacityNoteKind::IncompleteOccupation), 1);
	QVERIFY(hasNote(compositeResult, AreaCapacityNoteKind::IncompleteOccupation, "A", "", "3-B1"));

	// An unusable occupation outside the area is ignored.
	CapacityAnalysisTrain outside = train("A", 0.0, {occupation("X", 0.0, 20.0)});
	BlockingTimeDiagramInput incompleteZ = occupation("Z", 0.0, 20.0);
	incompleteZ.isComplete = false;
	outside.occupations.push_back(incompleteZ);
	const AreaCapacityResult outsideResult = analyzeAreaCapacity(areaInput({"X"}, {areaTrain(outside)}, 100.0));
	// unusable occupation: outside the area it gives no note
	QCOMPARE(countNotes(outsideResult, AreaCapacityNoteKind::IncompleteOccupation), 0);
	QVERIFY(outsideResult.complete);

	// Other traffic with an unusable occupation in the area is reported twice.
	AreaCapacityInput withOther = areaInput({"X"}, {areaTrain(train("A", 0.0, {occupation("X", 0.0, 20.0)}))}, 100.0);
	CapacityAnalysisTrain otherTrain = train("B", 0.0, {});
	otherTrain.occupations.push_back(incomplete);
	withOther.otherTraffic.push_back(areaTrain(otherTrain));
	const AreaCapacityResult otherResult = analyzeAreaCapacity(withOther);
	// unusable occupation: other traffic gets both notes
	QVERIFY(hasNote(otherResult, AreaCapacityNoteKind::IncompleteOccupation, "B", "", "X"));
	QVERIFY(hasNote(otherResult, AreaCapacityNoteKind::UnselectedTraffic, "B"));
	QVERIFY(!otherResult.complete);
}

void TestAreaCapacity::reportsUnselectedTraffic() {
	const AreaCapacityInput base = overlappingPair(100.0);
	const AreaCapacityResult alone = analyzeAreaCapacity(base);

	AreaCapacityInput request = base;
	request.otherTraffic.push_back(areaTrain(train("Z", 0.0, {occupation("X", 100.0, 110.0)})));
	request.otherTraffic.push_back(areaTrain(train("W", 0.0, {occupation("elsewhere", 0.0, 10.0)})));
	const AreaCapacityResult result = analyzeAreaCapacity(request);
	// unselected traffic: a train in the area makes the traffic incomplete
	QVERIFY(hasNote(result, AreaCapacityNoteKind::UnselectedTraffic, "Z"));
	QVERIFY(!result.complete);
	// unselected traffic: a train outside the area gives no note
	QCOMPARE(countNotes(result, AreaCapacityNoteKind::UnselectedTraffic), 1);
	const AreaCapacityResource row = rowFor(result, "X");
	const AreaCapacityResource baseRow = rowFor(alone, "X");
	// unselected traffic: the rows count the selected trains only
	QCOMPARE(row.trainCount, baseRow.trainCount);
	QCOMPARE(row.occupationCount, baseRow.occupationCount);
	QCOMPARE_EQ(row.occupationSeconds, baseRow.occupationSeconds);
	QCOMPARE_EQ(row.unionSeconds, baseRow.unionSeconds);

	AreaCapacityInput outsideOnly = base;
	outsideOnly.otherTraffic.push_back(areaTrain(train("W", 0.0, {occupation("elsewhere", 0.0, 10.0)})));
	const AreaCapacityResult outsideResult = analyzeAreaCapacity(outsideOnly);
	// unselected traffic: a train outside the area leaves the traffic complete
	QVERIFY(outsideResult.notes.empty());
	QVERIFY(outsideResult.complete);

	// A composite id touches the resource that it shares a component with.
	AreaCapacityInput compositeOther = areaInput({"2-B0"}, {areaTrain(train("A", 0.0, {occupation("2-B0", 0.0, 20.0)}))}, 100.0);
	compositeOther.otherTraffic.push_back(areaTrain(train("V", 0.0, {occupation("@2-B0@-1.0/@3-B1@-2.0", 0.0, 10.0)})));
	QVERIFY2(hasNote(analyzeAreaCapacity(compositeOther), AreaCapacityNoteKind::UnselectedTraffic, "V"),
		"unselected traffic: a composite id touches the area");

	// A selected composite occupation counts once in each row it belongs to and once in the envelope.
	const AreaCapacityResult composite = analyzeAreaCapacity(areaInput({"2-B0", "3-B1"},
		{areaTrain(train("A", 0.0, {occupation("@2-B0@-1.0/@3-B1@-2.0", 0.0, 10.0)}))}, 100.0));
	// composite id: once in each row
	QCOMPARE(rowFor(composite, "2-B0").occupationCount, 1);
	QVERIFY(near(rowFor(composite, "2-B0").occupationSeconds, 10.0));
	QCOMPARE(rowFor(composite, "3-B1").occupationCount, 1);
	QVERIFY(near(rowFor(composite, "3-B1").occupationSeconds, 10.0));
	QVERIFY2(near(composite.envelopeSeconds, 10.0), "composite id: once in the envelope");
}

void TestAreaCapacity::reportsPairWithoutSharedResource() {
	const AreaCapacityTrain first = areaTrain(train("A", 0.0, {occupation("X", 0.0, 20.0)}));
	const AreaCapacityTrain second = areaTrain(train("B", 20.0, {occupation("Y", 5.0, 25.0)}));
	const AreaCapacityResult result = analyzeAreaCapacity(areaInput({"X", "Y"}, {first, second}, 100.0, "B"));
	QVERIFY2(hasNote(result, AreaCapacityNoteKind::NoSharedResource, "A", "B"), "no shared resource: the note names the pair");
	// B shares nothing with A, so it starts with A and closes no cycle.
	// no shared resource: the chain is analysed without a cycle
	QVERIFY(result.chain.analyzable);
	QCOMPARE(result.chain.cycleEndIdentity, "B");
	QCOMPARE_LT(result.chain.cycleTime, 0.0);
	QCOMPARE(result.chain.compression.size(), std::size_t(2));
	QVERIFY(near(result.chain.compression[1].compressedReference, 0.0));
	// no shared resource: the rows are filled
	QCOMPARE(rowFor(result, "X").trainCount, 1);
	QCOMPARE(rowFor(result, "Y").trainCount, 1);
	QVERIFY(near(rowFor(result, "X").occupationSeconds, 20.0));
}

void TestAreaCapacity::reportsUnusableReference_data() {
	QTest::addColumn<double>("reference");
	QTest::newRow("negative") << -1.0;
	QTest::newRow("not a number") << std::numeric_limits<double>::quiet_NaN();
	QTest::newRow("infinite") << std::numeric_limits<double>::infinity();
}

void TestAreaCapacity::reportsUnusableReference() {
	QFETCH(double, reference);
	// B cannot be shifted, A is not shifted (both references 0), so the overlap stays.
	const AreaCapacityTrain first = areaTrain(train("A", 0.0, {occupation("X", 0.0, 20.0)}));
	const AreaCapacityTrain second = areaTrain(train("B", 20.0, {occupation("X", 5.0, 25.0)}, reference));
	const AreaCapacityResult result = analyzeAreaCapacity(areaInput({"X"}, {first, second}, 100.0, "B"));
	// unusable reference: the note names the train
	QCOMPARE(countNotes(result, AreaCapacityNoteKind::InvalidReference), 1);
	QVERIFY(hasNote(result, AreaCapacityNoteKind::InvalidReference, "B"));
	// unusable reference: the conflict remains
	QVERIFY(hasNote(result, AreaCapacityNoteKind::ChainConflict));
	QVERIFY(!result.chain.analyzable);
	// unusable reference: the pair is not reported as without shared resource
	QCOMPARE(countNotes(result, AreaCapacityNoteKind::NoSharedResource), 0);
}

// The same for an unusable reference of the leader.
void TestAreaCapacity::reportsUnusableReferenceOfLeader() {
	const AreaCapacityTrain badLeader = areaTrain(train("A", 0.0, {occupation("X", 0.0, 20.0)}, -1.0));
	const AreaCapacityTrain follower = areaTrain(train("B", 20.0, {occupation("X", 30.0, 40.0)}));
	const AreaCapacityResult leader = analyzeAreaCapacity(areaInput({"X"}, {badLeader, follower}, 100.0, "B"));
	// unusable reference: also for the leader
	QVERIFY(hasNote(leader, AreaCapacityNoteKind::InvalidReference, "A"));
	QCOMPARE(countNotes(leader, AreaCapacityNoteKind::NoSharedResource), 0);
}

void TestAreaCapacity::reportsDuplicateIdentity() {
	const AreaCapacityTrain firstEntry = areaTrain(train("A", 0.0, {occupation("X", 0.0, 20.0)}));
	const AreaCapacityTrain secondEntry = areaTrain(train("A", 20.0, {occupation("X", 5.0, 25.0)}));
	const AreaCapacityResult twice = analyzeAreaCapacity(areaInput({"X"}, {firstEntry, secondEntry}, 100.0));
	// duplicate identity: one note
	QCOMPARE(countNotes(twice, AreaCapacityNoteKind::DuplicateIdentity), 1);
	QVERIFY(hasNote(twice, AreaCapacityNoteKind::DuplicateIdentity, "A"));
	// duplicate identity: the first entry wins and the second is in no figure
	QCOMPARE(rowFor(twice, "X").trainCount, 1);
	QVERIFY(near(rowFor(twice, "X").occupationSeconds, 20.0));
	QCOMPARE(twice.chain.trainIdentities.size(), std::size_t(1));
	QVERIFY(near(twice.envelopeSeconds, 20.0));
	QVERIFY2(!twice.complete, "duplicate identity: the traffic is not complete");

	AreaCapacityInput other = areaInput({"X"}, {areaTrain(train("A", 0.0, {occupation("X", 0.0, 20.0)}))}, 100.0);
	other.otherTraffic.push_back(areaTrain(train("A", 0.0, {occupation("X", 50.0, 60.0)})));
	const AreaCapacityResult otherResult = analyzeAreaCapacity(other);
	// duplicate identity: other traffic with the id of a selected train
	QVERIFY(hasNote(otherResult, AreaCapacityNoteKind::DuplicateIdentity, "A"));
	QVERIFY(!otherResult.complete);
	// duplicate identity: the repeating entry is ignored everywhere
	QCOMPARE(countNotes(otherResult, AreaCapacityNoteKind::UnselectedTraffic), 0);
}

void TestAreaCapacity::marksGoverningRows() {
	// Equal sums: both rows govern, a smaller third row does not.
	const AreaCapacityResult equal = analyzeAreaCapacity(areaInput({"X", "Y", "Z"},
		{areaTrain(train("A", 0.0, {occupation("X", 0.0, 10.0), occupation("Y", 0.0, 10.0), occupation("Z", 0.0, 5.0)}))}, 100.0));
	// governing rows: equal sums are all kept
	QVERIFY(rowFor(equal, "X").governing);
	QVERIFY(rowFor(equal, "Y").governing);
	QVERIFY(!rowFor(equal, "Z").governing);

	// Sums 10 and 10.00000001 differ by 1e-8, below the tolerance of 1e-7.
	const AreaCapacityResult close = analyzeAreaCapacity(areaInput({"X", "Y"},
		{areaTrain(train("A", 0.0, {occupation("X", 0.0, 10.0), occupation("Y", 0.0, 10.00000001)}))}, 100.0));
	// governing rows: sums within the tolerance are all kept
	QVERIFY(rowFor(close, "X").governing);
	QVERIFY(rowFor(close, "Y").governing);

	// A sum that differs by more than the tolerance does not govern.
	const AreaCapacityResult apart = analyzeAreaCapacity(areaInput({"X", "Y"},
		{areaTrain(train("A", 0.0, {occupation("X", 0.0, 10.0), occupation("Y", 0.0, 10.000001)}))}, 100.0));
	// governing rows: a sum outside the tolerance does not govern
	QVERIFY(!rowFor(apart, "X").governing);
	QVERIFY(rowFor(apart, "Y").governing);

	// The largest sum 5e-8 is below the tolerance, so the unused row is within it, and still does not govern.
	const AreaCapacityResult tiny = analyzeAreaCapacity(
		areaInput({"X", "Y"}, {areaTrain(train("A", 0.0, {occupation("X", 0.0, 5e-8)}))}, 100.0));
	// governing rows: a row without occupation never governs
	QVERIFY(rowFor(tiny, "X").governing);
	QVERIFY(!rowFor(tiny, "Y").governing);

	// Without any occupation no row governs. The result does not depend on the period.
	const AreaCapacityResult none = analyzeAreaCapacity(areaInput({"X", "Y"}, {}, 0.0));
	// governing rows: no row governs without occupation
	QVERIFY(!rowFor(none, "X").governing);
	QVERIFY(!rowFor(none, "Y").governing);
	const AreaCapacityResult noPeriod = analyzeAreaCapacity(areaInput({"X", "Y"},
		{areaTrain(train("A", 0.0, {occupation("X", 0.0, 10.0), occupation("Y", 0.0, 4.0)}))}, -1.0));
	// governing rows: the period does not matter
	QVERIFY(rowFor(noPeriod, "X").governing);
	QVERIFY(!rowFor(noPeriod, "Y").governing);
}

void TestAreaCapacity::ignoresOccupationsOutsideArea() {
	const AreaCapacityResult result = analyzeAreaCapacity(areaInput({"X"},
		{areaTrain(train("A", 0.0, {occupation("X", 0.0, 20.0), occupation("Z", 0.0, 20.0)}))}, 100.0));
	// outside the area: the row counts the occupation in the area
	QCOMPARE(rowFor(result, "X").occupationCount, 1);
	QVERIFY(near(rowFor(result, "X").occupationSeconds, 20.0));
	// outside the area: the chain holds the occupations in the area only
	QCOMPARE(result.chain.compressedOccupations.size(), std::size_t(1));
	QCOMPARE(result.chain.compressedOccupations[0].size(), std::size_t(1));
	QVERIFY2(near(result.envelopeSeconds, 20.0), "outside the area: the envelope ignores them");
}

void TestAreaCapacity::leavesInputUnchanged() {
	BlockingTimeDiagramInput incomplete = occupation("X", 40.0, 50.0);
	incomplete.isComplete = false;
	AreaCapacityInput request = areaInput({"X", "Y", "X"},
		{areaTrain(train("A", 0.0, {occupation("X", 0.0, 20.0), occupation("Y", 0.0, 10.0), incomplete})),
			areaTrain(train("B", 20.0, {occupation("X", 5.0, 25.0)}), true),
			areaTrain(train("A", 30.0, {occupation("Y", 0.0, 10.0)}))},
		100.0, "B");
	request.otherTraffic.push_back(areaTrain(train("Z", 0.0, {occupation("Y", 60.0, 70.0)})));
	const AreaCapacityInput before = request;

	const AreaCapacityResult first = analyzeAreaCapacity(request);
	const AreaCapacityResult second = analyzeAreaCapacity(request);
	// input: the input is not changed
	QCOMPARE(request.resourceIds, before.resourceIds);
	QCOMPARE_EQ(request.periodSeconds, before.periodSeconds);
	QCOMPARE(request.cycleEndIdentity, before.cycleEndIdentity);
	QVERIFY(sameTrains(request.selected, before.selected));
	QVERIFY(sameTrains(request.otherTraffic, before.otherTraffic));
	// input: two calls give the same result
	QVERIFY(!first.notes.empty());
	QVERIFY(sameResult(first, second));
}

QTEST_APPLESS_MAIN(TestAreaCapacity)
#include "test_areacapacity.moc"
