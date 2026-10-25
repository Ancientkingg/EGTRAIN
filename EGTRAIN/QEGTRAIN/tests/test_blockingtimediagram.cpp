#include "diagrams/BlockingTimeDiagram.h"
#include "scene/SectionLabels.h"
#include "util/CsvWriter.h"

#include <QTest>

#include <limits>
#include <string>
#include <vector>

static BlockingTimeDiagramInput block(const char* id, double start, double end, double posStart, double posEnd, const char* switchName, const char* stationName, bool complete) {
	BlockingTimeDiagramInput b;
	b.blockId = id;
	b.startOccTime = start;
	b.endOccTime = end;
	b.posStart = posStart;
	b.posEnd = posEnd;
	b.switchName = switchName;
	b.stationName = stationName;
	b.isComplete = complete;
	b.endClearTime = end;
	return b;
}

static BlockingTimeDiagramSegment envelope(const char* id, double startKm, double endKm) {
	BlockingTimeDiagramSegment segment;
	segment.blockId = id;
	segment.startPositionKm = startKm;
	segment.endPositionKm = endKm;
	segment.midPositionKm = (startKm + endKm) / 2.0;
	return segment;
}

// Sections with and without finite ends or length.
static std::vector<BlockSectionExtent> brokenSections() {
	const double nan = std::numeric_limits<double>::quiet_NaN();
	const double inf = std::numeric_limits<double>::infinity();
	return buildBlockSectionExtents({{"@ok@", 0.0, 1.0}, {"@nan@", 1.0, nan}, {"@inf@", 1.0, inf}, {"@zero@", 2.0, 2.0},
		{"@tiny@", 3.0, 3.0 + 5e-10}, {"@last@", 4.0, 5.0}});
}

// The columns of the export. The times named "Original" are those that a compression does not move.
static std::vector<std::string> exportHeader() {
	return {"Train", "Block", "Occupation start[s]", "Occupation end[s]", "Position[km]", "Segment type", "Planned reference",
		"Station", "Planned time[s]", "Start X[km]", "End X[km]", "Original start[s]", "Original end[s]",
		"Original approach[s]", "Original run start[s]", "Original run end[s]", "Original clearance[s]", "Setup[s]",
		"Sight reaction[s]", "Release[s]", "Run margin[s]", "Layer", "Reference section", "Geo position[km]",
		"Start geo X[km]", "End geo X[km]"};
}

// The sections of a route that passes the resource r twice.
static std::vector<BlockSectionExtent> visits() {
	return buildBlockSectionExtents({{"@r@", 0.0, 2.0}, {"@other@", 2.0, 4.0}, {"@r@", 4.0, 6.0}, {"@bad@", 6.0, 6.0}});
}

static std::vector<BlockingTimeDiagramSegment> exampleSegments() {
	std::vector<std::vector<BlockingTimeDiagramInput>> trains = {
		{
			block("@12-B0@", 10.0, 20.0, 1000.0, 1400.0, "None", "None", true),
			block("@99-B0@", -1.0, 30.0, 1400.0, 1600.0, "None", "None", true),
			block("@13-B0@", 25.0, 40.0, 1600.0, 2200.0, "SW13", "None", true),
		},
		{
			block("@12-B0@", 15.0, 25.0, 1000.0, 1500.0, "None", "StationA", true),
			block("@40-B0@", 50.0, 45.0, 1800.0, 2100.0, "None", "None", true),
			block("@42-B0@", 45.0, 45.0, 1900.0, 2200.0, "None", "None", true),
			block("@41-B0@", 60.0, 70.0, 2100.0, 2300.0, "None", "None", false),
		},
	};
	std::vector<std::string> names = {"A", "B"};
	return buildBlockingTimeDiagramSegments(trains, names);
}

static const std::vector<BlockingTimePlannedReference> references = {
	{"A", "Origin", "departure", 10.0, 0.0},
	{"A", "Destination", "arrival", 30.0, 2.0},
	{"B", "Other", "departure", 40.0, 0.0}};

class TestBlockingTimeDiagram : public QObject {
	Q_OBJECT

private slots:
	void matchesExactResources();
	void buildsSegments();
	void scopesSegments();
	void scopesPlannedReferences();
	void touchingIsNoConflict();
	void keepsDirectedPositions();
	void dropsUnmeasuredEnvelopes();
	void keepsRouteSectionOrder();
	void keepsFallingSection();
	void keepsGap();
	void keepsRepeatedId();
	void listsSectionsWithoutGeometry();
	void keepsNegativeSection();
	void matchesSectionOfRoute();
	void matchesRepeatedId();
	void matchesCompositeSection();
	void skipsSectionWithoutGeometry();
	void keepsGeoPositionAndSwitch();
	void writesSectionRows();
	void notesSectionsWithoutGeometry();
	void notesBlocksNotShown();
	void scopesRepeatedSection();
	void keepsEnvelopesOfSelectedVisit();
	void keepsPlannedPositionsOfSelectedVisit();
	void rangesChooserIndices();
	void checksCapacityScope();
	void labelsWithoutInternalId();
	void shortensBlockIds();
	void marksSections();
};

void TestBlockingTimeDiagram::matchesExactResources() {
	QVERIFY2(!shareBlockingTimeResource("2-B0", "2-B4"), "distinct decorated block resources do not match");
	QVERIFY2(shareBlockingTimeResource("@2-B0@-1.0/@3-B1@-2.0", "2-B0"), "composite block matches its exact component");
}

void TestBlockingTimeDiagram::buildsSegments() {
	const std::vector<BlockingTimeDiagramSegment> segments = exampleSegments();
	// invalid and incomplete intervals are filtered
	QCOMPARE(segments.size(), std::size_t(3));
	// first segment train name
	QCOMPARE(segments[0].trainName, "A");
	// overlapping matching blocks are critical
	QCOMPARE(segments[0].style, BlockingTimeSegmentStyle::Critical);
	// first segment start time
	QCOMPARE_EQ(segments[0].startTime, 10.0);
	// first segment end time
	QCOMPARE_EQ(segments[0].endTime, 20.0);
	// first segment midpoint
	QCOMPARE_EQ(segments[0].midPositionKm, 1.2);
	// switch segment train name
	QCOMPARE(segments[1].trainName, "A");
	// switch segment style
	QCOMPARE(segments[1].style, BlockingTimeSegmentStyle::Switch);
	// directed rectangle endpoints retain their chart coordinates
	QCOMPARE_EQ(segments[1].startPositionKm, 1.6);
	QCOMPARE_EQ(segments[1].endPositionKm, 2.2);
	// critical station train name
	QCOMPARE(segments[2].trainName, "B");
	// critical station segment style
	QCOMPARE(segments[2].style, BlockingTimeSegmentStyle::CriticalStation);
	// critical station midpoint
	QCOMPARE_EQ(segments[2].midPositionKm, 1.25);
}

void TestBlockingTimeDiagram::scopesSegments() {
	const std::vector<BlockingTimeDiagramSegment> scoped = filterBlockingTimeDiagramSegments(
		exampleSegments(), {"A"}, {"@12-B0@"}, 12.0, 18.0);
	// route block and train scope filters segments
	QCOMPARE(scoped.size(), std::size_t(1));
	// time scope clips displayed bounds but retains original evidence
	QCOMPARE_EQ(scoped[0].startTime, 12.0);
	QCOMPARE_EQ(scoped[0].endTime, 18.0);
	QCOMPARE_EQ(scoped[0].originalStartTime, 10.0);
	QCOMPARE_EQ(scoped[0].originalEndTime, 20.0);
	// critical style survives when the conflicting train is hidden
	QCOMPARE(scoped[0].style, BlockingTimeSegmentStyle::Critical);
}

void TestBlockingTimeDiagram::scopesPlannedReferences() {
	const auto crossing = filterBlockingTimePlannedReferences(references, 15.0, 25.0);
	// planned scope retains both source points for a visible line crossing
	QCOMPARE(crossing.size(), std::size_t(2));
	QCOMPARE_EQ(crossing.front().time, 10.0);
	QCOMPARE_EQ(crossing.back().time, 30.0);
	const auto plotted = clipBlockingTimePlannedReferences(references, 15.0, 25.0);
	// selected window clips rendered planned line without presenting boundary as station events
	QCOMPARE(plotted.size(), std::size_t(1));
	QCOMPARE(plotted[0].size(), std::size_t(2));
	QCOMPARE_EQ(plotted[0][0].time, 15.0);
	QCOMPARE_EQ(plotted[0][1].time, 25.0);
	QCOMPARE_EQ(plotted[0][0].positionKm, 0.5);
	QCOMPARE_EQ(plotted[0][1].positionKm, 1.5);
	QVERIFY(plotted[0][0].stationName.empty());
	QVERIFY(plotted[0][1].stationName.empty());
	QCOMPARE(plotted[0][0].eventType, "clipped planned interpolation");
	const auto gaps = clipBlockingTimePlannedReferences({references[0],
															{"A", "", "", std::numeric_limits<double>::quiet_NaN(), 0.0}, references[1]},
		15.0, 25.0);
	QVERIFY2(gaps.empty(), "omitted planned events cannot be bridged by a rendered line");
	QVERIFY2(filterBlockingTimePlannedReferences(references, 31.0, 39.0).empty(),
		"off-window planned points do not create an empty scoped chart or export");
}

void TestBlockingTimeDiagram::touchingIsNoConflict() {
	const auto nearTouch = buildBlockingTimeDiagramSegments({{block("T", 0.0, 10.00000005, 0.0, 100.0, "None", "None", true)},
																{block("T", 10.0, 20.0, 0.0, 100.0, "None", "None", true)}},
		{"A", "B"});
	// sub-tolerance touching is not also styled as an overlap conflict
	QCOMPARE(nearTouch.size(), std::size_t(2));
	QCOMPARE(nearTouch[0].style, BlockingTimeSegmentStyle::Default);
	QCOMPARE(nearTouch[1].style, BlockingTimeSegmentStyle::Default);
}

void TestBlockingTimeDiagram::keepsDirectedPositions() {
	const auto reversed = buildBlockingTimeDiagramSegments({{block("reverse", 1, 2, 2000, 1000, "None", "None", true),
															   block("point", 2, 3, 1000, 1000, "None", "None", true)}},
		{"R"});
	// reversed and zero-width boundaries preserve exact directed positions
	QCOMPARE(reversed.size(), std::size_t(2));
	QCOMPARE_EQ(reversed[0].startPositionKm, 2.0);
	QCOMPARE_EQ(reversed[0].endPositionKm, 1.0);
	QCOMPARE_EQ(reversed[1].startPositionKm, reversed[1].endPositionKm);
}

void TestBlockingTimeDiagram::dropsUnmeasuredEnvelopes() {
	auto missing = block("missing clearance", 1, 2, 0, 100, "None", "None", true);
	missing.endClearTime = -1;
	auto nonfinite = block("nonfinite", 1, 2, 0, 100, "None", "None", true);
	nonfinite.posEnd = std::numeric_limits<double>::infinity();
	auto nonfiniteClearance = block("nonfinite clearance", 1, 2, 0, 100, "None", "None", true);
	nonfiniteClearance.endClearTime = std::numeric_limits<double>::infinity();
	QVERIFY2(buildBlockingTimeDiagramSegments({{missing, nonfinite, nonfiniteClearance}}, {"R"}).empty(),
		"missing clearance and nonfinite geometry cannot be rendered as measured envelopes");
}

void TestBlockingTimeDiagram::keepsRouteSectionOrder() {
	// Synthetic endpoints shaped like Assignment_Gvc_Gdg_Ut route0: 32 blocks of 2 km from 0 km.
	// tools/e2e/csv_export_smoke.sh checks the real endpoints of that scene against infrastructure.json.
	std::vector<BlockSectionInput> assignment;
	for (int k = 0; k < 32; ++k)
		assignment.push_back({"@" + std::to_string(k) + "-B0@", 2.0 * k, 2.0 * k + 2.0});
	const auto assignmentExtents = buildBlockSectionExtents(assignment);
	// route sections keep their order, ids and ends
	QCOMPARE(assignmentExtents.size(), std::size_t(32));
	bool exact = true;
	for (std::size_t k = 0; exact && k < assignmentExtents.size(); ++k)
		exact = assignmentExtents[k].order == static_cast<int>(k) + 1 && assignmentExtents[k].id == assignment[k].id
			&& assignmentExtents[k].startKm == 2.0 * static_cast<double>(k)
			&& assignmentExtents[k].endKm == 2.0 * static_cast<double>(k) + 2.0 && assignmentExtents[k].geometryValid
			&& assignmentExtents[k].issue.empty();
	QVERIFY2(exact, "route sections keep their order, ids and ends");
}

void TestBlockingTimeDiagram::keepsFallingSection() {
	const auto falling = buildBlockSectionExtents({{"@a@", 10.0, 8.0}, {"@b@", 8.0, 6.0}});
	// a section towards decreasing X stays valid and is not swapped
	QCOMPARE(falling.size(), std::size_t(2));
	QVERIFY(falling[0].geometryValid);
	QVERIFY(falling[1].geometryValid);
	QCOMPARE_EQ(falling[0].startKm, 10.0);
	QCOMPARE_EQ(falling[0].endKm, 8.0);
	QCOMPARE_EQ(falling[0].lowerKm(), 8.0);
	QCOMPARE_EQ(falling[0].upperKm(), 10.0);
	QCOMPARE_EQ(falling[1].startKm, 8.0);
	QCOMPARE_EQ(falling[1].endKm, 6.0);
}

void TestBlockingTimeDiagram::keepsGap() {
	const auto gap = buildBlockSectionExtents({{"@a@", 0.0, 1.0}, {"@b@", 3.0, 4.0}});
	// a gap between sections is kept, not closed
	QCOMPARE(gap.size(), std::size_t(2));
	QCOMPARE_EQ(gap[0].endKm, 1.0);
	QCOMPARE_EQ(gap[1].startKm, 3.0);
	QCOMPARE_EQ(gap[1].endKm, 4.0);
}

void TestBlockingTimeDiagram::keepsRepeatedId() {
	const auto repeated = buildBlockSectionExtents({{"@a@", 0.0, 1.0}, {"@a@", 5.0, 6.0}});
	// a repeated id is not collapsed
	QCOMPARE(repeated.size(), std::size_t(2));
	QCOMPARE(repeated[0].order, 1);
	QCOMPARE(repeated[1].order, 2);
	QCOMPARE_EQ(repeated[1].startKm, 5.0);
}

void TestBlockingTimeDiagram::listsSectionsWithoutGeometry() {
	const auto broken = brokenSections();
	// sections without finite ends or length are listed without geometry
	QCOMPARE(broken.size(), std::size_t(6));
	QVERIFY(broken[0].geometryValid);
	QVERIFY(!broken[1].geometryValid);
	QCOMPARE(broken[1].issue, "no valid position");
	QVERIFY(!broken[2].geometryValid);
	QCOMPARE(broken[2].issue, "no valid position");
	QVERIFY(!broken[3].geometryValid);
	QCOMPARE(broken[3].issue, "zero length");
	QVERIFY(!broken[4].geometryValid);
	QCOMPARE(broken[4].issue, "zero length");
	QVERIFY(broken[5].geometryValid);
	// sections without geometry keep their place in the order
	QCOMPARE(broken[1].order, 2);
	QCOMPARE(broken[2].order, 3);
	QCOMPARE(broken[3].order, 4);
	QCOMPARE(broken[4].order, 5);
	QCOMPARE(broken[5].order, 6);
	QCOMPARE(broken[1].id, "@nan@");
	QVERIFY2(!blockSectionBounds({broken[1], broken[3]}), "missing geometry yields no invented coordinate bounds");
}

void TestBlockingTimeDiagram::keepsNegativeSection() {
	const auto negative = buildBlockSectionExtents({{"@n@", -1.5, -0.5}});
	// a negative finite section is valid
	QCOMPARE(negative.size(), std::size_t(1));
	QVERIFY(negative[0].geometryValid);
	QCOMPARE_EQ(negative[0].startKm, -1.5);
	QCOMPARE_EQ(negative[0].endKm, -0.5);
	QVERIFY2(buildBlockSectionExtents({}).empty(), "no route sections give no extents");
}

void TestBlockingTimeDiagram::matchesSectionOfRoute() {
	const std::vector<BlockSectionExtent> route = buildBlockSectionExtents({{"@0-B0@", 0.0, 2.0}, {"@1-B0@", 2.0, 4.0}, {"@2-B0@", 4.0, 6.0}});
	std::vector<BlockingTimeDiagramSegment> same = {envelope("@2-B0@", 4.0, 6.0)};
	matchBlockingSegmentsToSections(same, route, "route0");
	// same id and interval gives the section and no note, as the hover names the block
	QCOMPARE(same[0].referenceSection, 3);
	QVERIFY(same[0].referenceNote.empty());

	std::vector<BlockingTimeDiagramSegment> opposite = {envelope("@1-B0@", 4.0, 2.0)};
	matchBlockingSegmentsToSections(opposite, route, "route0");
	// an envelope from high X to low X matches the same section
	QCOMPARE(opposite[0].referenceSection, 2);
	QVERIFY(opposite[0].referenceNote.empty());

	std::vector<BlockingTimeDiagramSegment> otherId = {envelope("@9-B0@", 2.0, 4.0)};
	matchBlockingSegmentsToSections(otherId, route, "route0");
	// the same interval with another id is not a section of the route
	QCOMPARE(otherId[0].referenceSection, 0);
	QCOMPARE(otherId[0].referenceNote, "Not on route0");

	std::vector<BlockingTimeDiagramSegment> point = {envelope("@1-B0@", 3.0, 3.0)};
	matchBlockingSegmentsToSections(point, route, "route0");
	// a zero-length record is a location and stays unevaluated
	QCOMPARE(point[0].referenceSection, 0);
	QVERIFY(point[0].referenceNote.empty());

	std::vector<BlockingTimeDiagramSegment> none = {envelope("@1-B0@", 2.0, 4.0)};
	matchBlockingSegmentsToSections(none, {}, "route0");
	// no sections evaluate nothing
	QCOMPARE(none[0].referenceSection, 0);
	QVERIFY(none[0].referenceNote.empty());

	same[0].startPositionKm = same[0].endPositionKm;
	matchBlockingSegmentsToSections(same, route, "route0");
	// point rematch clears stale section metadata
	QCOMPARE(same[0].referenceSection, 0);
	QVERIFY(same[0].referenceNote.empty());
}

void TestBlockingTimeDiagram::matchesRepeatedId() {
	const auto twice = buildBlockSectionExtents({{"@x@", 0.0, 1.0}, {"@x@", 5.0, 6.0}});
	std::vector<BlockingTimeDiagramSegment> second = {envelope("@x@", 5.0, 6.0)};
	matchBlockingSegmentsToSections(second, twice, "route0");
	// a repeated id matches the occurrence with the equal interval
	QCOMPARE(second[0].referenceSection, 2);
	QVERIFY(second[0].referenceNote.empty());
	std::vector<BlockingTimeDiagramSegment> overlapping = {envelope("@x@", 0.5, 1.5), envelope("@x@", 5.5, 8.0)};
	matchBlockingSegmentsToSections(overlapping, twice, "route0");
	// an envelope of the same id between the intervals shares a block with the first section it overlaps
	QCOMPARE(overlapping[0].referenceSection, 1);
	QCOMPARE(overlapping[0].referenceNote, "Section 1 of 2: x");
	QCOMPARE(overlapping[1].referenceSection, 2);
	QCOMPARE(overlapping[1].referenceNote, "Section 2 of 2: x");

	const auto duplicated = buildBlockSectionExtents({{"@d@", 0.0, 1.0}, {"@d@", 0.0, 1.0}});
	std::vector<BlockingTimeDiagramSegment> first = {envelope("@d@", 0.0, 1.0)};
	matchBlockingSegmentsToSections(first, duplicated, "route0");
	// coincident occurrences are retained but cannot identify one envelope occurrence, and the note names them one per line
	QCOMPARE(first[0].referenceSection, 0);
	QCOMPARE(first[0].referenceNote, "Matches several sections of route0\nSection 1 of 2: d\nSection 2 of 2: d");
	QCOMPARE(duplicated.size(), std::size_t(2));
	std::vector<BlockingTimeDiagramSegment> sharedMany = {envelope("@d@", 0.2, 0.8)};
	matchBlockingSegmentsToSections(sharedMany, duplicated, "route0");
	// shared-resource overlap with several occurrences is ambiguous
	QCOMPARE(sharedMany[0].referenceSection, 0);
	QCOMPARE(sharedMany[0].referenceNote.find("Matches several sections"), std::size_t(0));
	matchBlockingSegmentsToSections(second, {}, "route0");
	// empty route rematch clears stale section metadata
	QCOMPARE(second[0].referenceSection, 0);
	QVERIFY(second[0].referenceNote.empty());
}

void TestBlockingTimeDiagram::matchesCompositeSection() {
	const auto composite = buildBlockSectionExtents({{"@3-B157@-59.120000/@34-B88@-59.130000", 10.0, 10.5}});
	std::vector<BlockingTimeDiagramSegment> plain = {envelope("@34-B88@", 10.0, 10.5)};
	matchBlockingSegmentsToSections(plain, composite, "route32");
	// a plain id matches the composite section that holds its block
	QCOMPARE(plain[0].referenceSection, 1);
	QCOMPARE(plain[0].referenceNote, "Section 1 of 1: 3-B157/34-B88");
	std::vector<BlockingTimeDiagramSegment> apart = {envelope("@34-B88@", 11.0, 12.0)};
	matchBlockingSegmentsToSections(apart, composite, "route32");
	// a shared block without an overlapping interval is not matched
	QCOMPARE(apart[0].referenceSection, 0);
}

// A section without geometry is never a candidate, even when its ends would be equal.
void TestBlockingTimeDiagram::skipsSectionWithoutGeometry() {
	const auto unusable = buildBlockSectionExtents({{"@t@", 0.0, 5e-10}});
	std::vector<BlockingTimeDiagramSegment> tiny = {envelope("@t@", 0.0, 5e-10)};
	matchBlockingSegmentsToSections(tiny, unusable, "route0");
	// a section without geometry is not a candidate
	QCOMPARE(tiny[0].referenceSection, 0);
	QCOMPARE(tiny[0].referenceNote, "Not on route0");
}

// The geo X of the ends and the switch flag pass through as the route holds them. Without the geo X of an end the
// section takes its route distance.
void TestBlockingTimeDiagram::keepsGeoPositionAndSwitch() {
	const double nan = std::numeric_limits<double>::quiet_NaN();
	const auto geo = buildBlockSectionExtents({{"@w@", 4.0, 5.0, 36.5, 35.5, true}, {"@p@", 5.0, 6.0}, {"@h@", 6.0, 7.0, 34.5, nan}});
	// sections keep their geo X and switch flag, and fall back to the route distance without geo X
	QCOMPARE(geo.size(), std::size_t(3));
	QCOMPARE_EQ(geo[0].geoStartKm, 36.5);
	QCOMPARE_EQ(geo[0].geoEndKm, 35.5);
	QVERIFY(geo[0].isSwitch);
	QCOMPARE_EQ(geo[1].geoStartKm, 5.0);
	QCOMPARE_EQ(geo[1].geoEndKm, 6.0);
	QVERIFY(!geo[1].isSwitch);
	QCOMPARE_EQ(geo[2].geoStartKm, 6.0);
	QCOMPARE_EQ(geo[2].geoEndKm, 7.0);
}

// Rows of the export.
void TestBlockingTimeDiagram::writesSectionRows() {
	const std::vector<std::string> header = exportHeader();
	// the export header has the 26 columns in order
	QCOMPARE(blockingTimeCsvHeader(), header);

	const double nan = std::numeric_limits<double>::quiet_NaN();
	const auto exported = buildBlockSectionExtents({{"@0-B0@", 0.0, 2.0}, {"@bad@", 2.0, nan}, {"@fall@", 0.5, 0.23, 36.5, 35.5}});
	const auto rows = blockSectionCsvRows(exported);
	// every section row is as wide as the header
	QCOMPARE(rows.size(), std::size_t(3));
	QCOMPARE(rows[0].size(), header.size());
	QCOMPARE(rows[1].size(), header.size());
	QCOMPARE(rows[2].size(), header.size());
	// a section row holds its id, midpoint, ends, layer, order and geo X
	QCOMPARE(rows[0][1], "@0-B0@");
	QCOMPARE(rows[0][4], "1");
	QCOMPARE(rows[0][5], "block section");
	QCOMPARE(rows[0][9], "0");
	QCOMPARE(rows[0][10], "2");
	QCOMPARE(rows[0][21], "block section extent");
	QCOMPARE(rows[0][22], "1");
	QCOMPARE(rows[0][23], "1");
	QCOMPARE(rows[0][24], "0");
	QCOMPARE(rows[0][25], "2");
	bool emptyElsewhere = true;
	for (std::size_t column = 0; column < header.size(); ++column)
		if (column != 1 && column != 4 && column != 5 && column != 9 && column != 10 && column != 21 && column != 22
			&& column < 23)
			emptyElsewhere = emptyElsewhere && rows[0][column].empty() && rows[1][column].empty()
				&& rows[2][column].empty();
	QVERIFY2(emptyElsewhere, "a section row leaves train, times and components empty");
	// a section without geometry has no coordinates and its own layer
	QCOMPARE(rows[1][1], "@bad@");
	QVERIFY(rows[1][4].empty());
	QVERIFY(rows[1][9].empty());
	QVERIFY(rows[1][10].empty());
	QCOMPARE(rows[1][5], "block section");
	QCOMPARE(rows[1][21], "block section extent (geometry missing)");
	QCOMPARE(rows[1][22], "2");
	QVERIFY(rows[1][23].empty());
	QVERIFY(rows[1][24].empty());
	QVERIFY(rows[1][25].empty());
	// a falling section is written as the route holds it
	QCOMPARE(rows[2][9], csv::formatDouble(0.5));
	QCOMPARE(rows[2][10], csv::formatDouble(0.23));
	QCOMPARE(rows[2][4], csv::formatDouble((0.5 + 0.23) / 2.0));
	QCOMPARE(rows[2][22], "3");
	// a section against the coordinate gives its geo X from start to end
	QCOMPARE(rows[2][23], "36");
	QCOMPARE(rows[2][24], "36.5");
	QCOMPARE(rows[2][25], "35.5");
	QVERIFY2(blockSectionCsvRows({}).empty(), "no sections give no rows");
}

// Note of the sections without geometry.
void TestBlockingTimeDiagram::notesSectionsWithoutGeometry() {
	const auto sections = [](int count, int invalid) {
		std::vector<BlockSectionInput> inputs;
		for (int i = 0; i < count; ++i)
			inputs.push_back({"@s" + std::to_string(i) + "@", static_cast<double>(i), i < invalid ? static_cast<double>(i) : i + 1.0});
		return buildBlockSectionExtents(inputs);
	};
	// usable extents give no note
	QVERIFY(blockSectionGeometryNote(sections(4, 0)).empty());
	QVERIFY(blockSectionGeometryNote({}).empty());
	// one section without geometry is named by its short id
	QCOMPARE(blockSectionGeometryNote(sections(4, 1)), "Sections not drawn (no valid length): s0");
	// three sections without geometry are all named
	QCOMPARE(blockSectionGeometryNote(sections(4, 3)), "Sections not drawn (no valid length): s0, s1, s2");
	// more than three sections without geometry end with the number left out
	QCOMPARE(blockSectionGeometryNote(sections(5, 4)), "Sections not drawn (no valid length): s0, s1, s2 and 1 more");
	const double nan = std::numeric_limits<double>::quiet_NaN();
	const auto mixed = buildBlockSectionExtents({{"@a@", 0.0, 1.0}, {"@b@", 1.0, 1.0}, {"@c@", 1.0, 2.0}, {"@d@", 2.0, nan}});
	// the note lists the sections without geometry in route order and no others
	QCOMPARE(blockSectionGeometryNote(mixed), "Sections not drawn (no valid length): b, d");
}

// The note above the chart has only the parts that apply, and no count of 0.
void TestBlockingTimeDiagram::notesBlocksNotShown() {
	QVERIFY2(blockingTimeNote(0, false, false, "route0").empty(), "nothing missing gives no note");
	QVERIFY2(blockingTimeNote(0, false, true, "route0").empty(), "blocks that were not placed say nothing while the chart has some");
	// one block or stop that is not shown is said in the singular
	QCOMPARE(blockingTimeNote(1, false, true, "route0"), "1 block or stop could not be placed on route0 and is not shown.");
	// the count of the blocks or stops that are not shown
	QCOMPARE(blockingTimeNote(3, false, true, "route0"), "3 blocks or stops could not be placed on route0 and are not shown.");
	// an empty chart without blocks that were not placed has nothing in its section and period
	QCOMPARE(blockingTimeNote(0, true, false, "route0"), "No complete blocking times in this section and period.");
	// an empty chart whose blocks were not placed says so after the count
	QCOMPARE(blockingTimeNote(2, true, true, "route0"),
		"2 blocks or stops could not be placed on route0 and are not shown. No blocking times could be placed on route0.");
}

void TestBlockingTimeDiagram::scopesRepeatedSection() {
	const auto returning = buildBlockSectionExtents({{"@a@", 2, 8}, {"@b@", 8, -2}, {"@a@", -2, 2}});
	QVERIFY2(blockSectionBounds(returning) == std::make_optional(std::make_pair(-2.0, 8.0)),
		"returning route includes intermediate endpoints, not only first/last");
	const auto selectedRepeat = scopedBlockSectionExtents(returning, {2});
	// scope selects repeated occurrence by index and retains original order
	QCOMPARE(selectedRepeat.size(), std::size_t(1));
	QCOMPARE(selectedRepeat[0].order, 3);
	QCOMPARE_EQ(selectedRepeat[0].startKm, -2.0);
}

// A repeated id selected by index: only the interval of the chosen visit counts.
void TestBlockingTimeDiagram::keepsEnvelopesOfSelectedVisit() {
	const auto secondVisit = scopedBlockSectionExtents(visits(), {2});
	// an envelope on the selected visit of a repeated id is kept in either direction
	QVERIFY(blockingSegmentInSections(envelope("@r@", 4.5, 5.5), secondVisit));
	QVERIFY(blockingSegmentInSections(envelope("@r@", 5.5, 4.5), secondVisit));
	QVERIFY2(!blockingSegmentInSections(envelope("@r@", 0.5, 1.5), secondVisit),
		"an envelope on the other visit of the same id is dropped");
	QVERIFY2(blockingSegmentInSections(envelope("@r@", 0.5, 1.5), scopedBlockSectionExtents(visits(), {0})),
		"the same envelope is kept when the first visit is selected");
	QVERIFY2(!blockingSegmentInSections(envelope("@other@", 4.5, 5.5), secondVisit),
		"an envelope of another resource on the selected interval is dropped");
	// an envelope that only touches the selected interval is dropped
	QVERIFY(!blockingSegmentInSections(envelope("@r@", 6.0, 7.0), secondVisit));
	QVERIFY(!blockingSegmentInSections(envelope("@r@", 3.0, 4.0), secondVisit));
	// a zero-length envelope is kept only inside the selected interval
	QVERIFY(blockingSegmentInSections(envelope("@r@", 5.0, 5.0), secondVisit));
	QVERIFY(!blockingSegmentInSections(envelope("@r@", 1.0, 1.0), secondVisit));
	// a section without geometry and an empty scope hold no envelope
	QVERIFY(!blockingSegmentInSections(envelope("@bad@", 6.0, 6.0), scopedBlockSectionExtents(visits(), {3})));
	QVERIFY(!blockingSegmentInSections(envelope("@r@", 4.5, 5.5), {}));
}

void TestBlockingTimeDiagram::keepsPlannedPositionsOfSelectedVisit() {
	const auto secondVisit = scopedBlockSectionExtents(visits(), {2});
	// a planned position on the selected visit, ends included, is inside the scope
	QVERIFY(positionInBlockSections(5.0, secondVisit));
	QVERIFY(positionInBlockSections(4.0, secondVisit));
	QVERIFY(positionInBlockSections(6.0, secondVisit));
	// a planned position on the other visit of the same id is outside the scope and becomes a gap
	QVERIFY(!positionInBlockSections(1.0, secondVisit));
	QVERIFY(!positionInBlockSections(3.9, secondVisit));
	QVERIFY(!positionInBlockSections(1.0, {}));
	QVERIFY2(!positionInBlockSections(6.0, scopedBlockSectionExtents(visits(), {3})),
		"a section without geometry holds no planned position");
}

void TestBlockingTimeDiagram::rangesChooserIndices() {
	// the chooser range is zero-based, inclusive and in either order
	QCOMPARE(blockSectionRangeIndices(1, 3, 5), std::vector<int>({1, 2, 3}));
	QCOMPARE(blockSectionRangeIndices(3, 1, 5), std::vector<int>({1, 2, 3}));
	QCOMPARE(blockSectionRangeIndices(2, 2, 5), std::vector<int>({2}));
	// the chooser range stops at the last section and is empty without sections
	QCOMPARE(blockSectionRangeIndices(2, 9, 4), std::vector<int>({2, 3}));
	QVERIFY(blockSectionRangeIndices(-1, -1, 3).empty());
	QVERIFY(blockSectionRangeIndices(0, 0, 0).empty());
}

void TestBlockingTimeDiagram::checksCapacityScope() {
	const auto returning = buildBlockSectionExtents({{"@a@", 2, 8}, {"@b@", 8, -2}, {"@a@", -2, 2}});
	QVERIFY2(!blockSectionCapacityScopeIssue(returning, {0}).empty(),
		"capacity rejects a selected repeated resource with another occurrence outside scope");
	QVERIFY2(blockSectionCapacityScopeIssue(returning, {0, 1, 2}).empty(), "a resource repeated only inside the chosen blocks is accepted");
	const auto unrelatedRepeat = buildBlockSectionExtents({{"@entry@", 0, 1}, {"@else@", 1, 2}, {"@else@", 2, 3}});
	QVERIFY2(blockSectionCapacityScopeIssue(unrelatedRepeat, {0}).empty(), "unrelated repeats do not block a representable capacity scope");
	const auto sharedScope = buildBlockSectionExtents({{"@a@/@b@", 0, 1}, {"@b@", 2, 3}});
	QVERIFY2(!blockSectionCapacityScopeIssue(sharedScope, {0}).empty(), "capacity rejects shared composite resource outside selected scope");
}

// A label for user text has no internal id: a shared label ends with the short id, and a block without a label is its
// short id.
void TestBlockingTimeDiagram::labelsWithoutInternalId() {
	SceneSectionLabels readable;
	readable.byReference = {{"@12-B0@", "B0 24.000 to 26.000 km"}, {"@4-B13@-33.570000/@3-B14@-33.620000", "B13 33.570 km (@4-B13@-33.570000/@3-B14@-33.620000)"}};
	// a readable label holds no internal id
	QCOMPARE(readableSectionLabel("@12-B0@", readable), "B0 24.000 to 26.000 km");
	QCOMPARE(readableSectionLabel("@4-B13@-33.570000/@3-B14@-33.620000", readable), "B13 33.570 km (4-B13/3-B14)");
	QCOMPARE(readableSectionLabel("@5-B11@", readable), "5-B11");
}

void TestBlockingTimeDiagram::shortensBlockIds() {
	// the short id joins the blocks of a switch section with a slash and keeps a plain id
	QCOMPARE(shortBlockId("@71-B0@-21.235000/@0-B1@-21.252000"), "71-B0/0-B1");
	QCOMPARE(shortBlockId("@35-B0@"), "35-B0");
	QCOMPARE(shortBlockId("plain"), "plain");
	QVERIFY(shortBlockId("").empty());
}

// The station, critical and conflict marks of the section strip.
void TestBlockingTimeDiagram::marksSections() {
	const auto extents = buildBlockSectionExtents({{"@a@", 0, 1}, {"@b@", 1, 2}, {"@c@", 2, 3}, {"@d@", 3, 4}});
	BlockingTimeDiagramSegment critical = envelope("@b@", 1, 2);
	critical.referenceSection = 2;
	critical.capacityCritical = true;
	BlockingTimeDiagramSegment conflict = envelope("@c@", 2, 3);
	conflict.referenceSection = 3;
	conflict.style = BlockingTimeSegmentStyle::CriticalStation;
	BlockingTimeDiagramSegment plain = envelope("@d@", 3, 4);
	plain.referenceSection = 4;
	plain.style = BlockingTimeSegmentStyle::Station;
	const auto compressed = blockSectionMarks(extents, {critical, conflict, plain}, {0.5, 4.0}, true);
	// a section holding a station of the route, ends included, is a station section
	QCOMPARE(compressed.size(), std::size_t(4));
	QVERIFY(compressed[0].station);
	QVERIFY(!compressed[1].station);
	QVERIFY(!compressed[2].station);
	QVERIFY(compressed[3].station);
	// a capacity-critical blocking time of a compressed chart marks its section critical
	QVERIFY(compressed[1].critical);
	QVERIFY(!compressed[1].conflict);
	QVERIFY(!compressed[0].critical);
	QVERIFY(!compressed[3].critical);
	// a conflicting blocking time marks its section
	QVERIFY(compressed[2].conflict);
	QVERIFY(!compressed[3].conflict);
	const auto ordinary = blockSectionMarks(extents, {critical, conflict}, {}, false);
	// an ordinary chart has no critical block
	QVERIFY(!ordinary[1].critical);
	QVERIFY(!ordinary[1].conflict);
	QVERIFY(ordinary[2].conflict);
	QVERIFY(!ordinary[0].station);
}

QTEST_APPLESS_MAIN(TestBlockingTimeDiagram)
#include "test_blockingtimediagram.moc"
