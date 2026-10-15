#include "diagrams/BlockingTimeDiagram.h"
#include "util/CsvWriter.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

static bool expect(bool condition, const char* message) {
	if (!condition)
		std::cerr << "failed: " << message << "\n";
	return condition;
}

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

static bool blockSectionTests() {
	bool ok = true;
	const double nan = std::numeric_limits<double>::quiet_NaN();
	const double inf = std::numeric_limits<double>::infinity();

	// Known endpoints: the sections of Assignment_Gvc_Gdg_Ut route0, 32 blocks of 2 km from 0 km.
	std::vector<BlockSectionInput> assignment;
	for (int k = 0; k < 32; ++k)
		assignment.push_back({"@" + std::to_string(k) + "-B0@", 2.0 * k, 2.0 * k + 2.0});
	const auto assignmentExtents = buildBlockSectionExtents(assignment);
	bool exact = assignmentExtents.size() == 32;
	for (std::size_t k = 0; exact && k < assignmentExtents.size(); ++k)
		exact = assignmentExtents[k].order == static_cast<int>(k) + 1 && assignmentExtents[k].id == assignment[k].id
			&& assignmentExtents[k].startKm == 2.0 * static_cast<double>(k)
			&& assignmentExtents[k].endKm == 2.0 * static_cast<double>(k) + 2.0 && assignmentExtents[k].geometryValid
			&& assignmentExtents[k].issue.empty();
	ok &= expect(exact, "route sections keep their order, ids and ends");

	const auto falling = buildBlockSectionExtents({{"@a@", 10.0, 8.0}, {"@b@", 8.0, 6.0}});
	ok &= expect(falling.size() == 2 && falling[0].geometryValid && falling[1].geometryValid
			&& falling[0].startKm == 10.0 && falling[0].endKm == 8.0 && falling[0].lowerKm() == 8.0
			&& falling[0].upperKm() == 10.0 && falling[1].startKm == 8.0 && falling[1].endKm == 6.0,
		"a section towards decreasing X stays valid and is not swapped");

	const auto gap = buildBlockSectionExtents({{"@a@", 0.0, 1.0}, {"@b@", 3.0, 4.0}});
	ok &= expect(gap.size() == 2 && gap[0].endKm == 1.0 && gap[1].startKm == 3.0 && gap[1].endKm == 4.0,
		"a gap between sections is kept, not closed");

	const auto repeated = buildBlockSectionExtents({{"@a@", 0.0, 1.0}, {"@a@", 5.0, 6.0}});
	ok &= expect(repeated.size() == 2 && repeated[0].order == 1 && repeated[1].order == 2 && repeated[1].startKm == 5.0,
		"a repeated id is not collapsed");

	const auto broken = buildBlockSectionExtents({{"@ok@", 0.0, 1.0}, {"@nan@", 1.0, nan}, {"@inf@", 1.0, inf}, {"@zero@", 2.0, 2.0},
		{"@tiny@", 3.0, 3.0 + 5e-10}, {"@last@", 4.0, 5.0}});
	ok &= expect(broken.size() == 6 && broken[0].geometryValid && !broken[1].geometryValid
			&& broken[1].issue == "coordinate is not finite" && !broken[2].geometryValid
			&& broken[2].issue == "coordinate is not finite" && !broken[3].geometryValid && broken[3].issue == "zero length"
			&& !broken[4].geometryValid && broken[4].issue == "zero length" && broken[5].geometryValid,
		"sections without finite ends or length are listed without geometry");
	ok &= expect(broken[1].order == 2 && broken[2].order == 3 && broken[3].order == 4 && broken[4].order == 5
			&& broken[5].order == 6 && broken[1].id == "@nan@",
		"sections without geometry keep their place in the order");

	const auto negative = buildBlockSectionExtents({{"@n@", -1.5, -0.5}});
	ok &= expect(negative.size() == 1 && negative[0].geometryValid && negative[0].startKm == -1.5
			&& negative[0].endKm == -0.5,
		"a negative finite section is valid");
	ok &= expect(buildBlockSectionExtents({}).empty(), "no route sections give no extents");

	// Matching.
	const std::vector<BlockSectionExtent> route = buildBlockSectionExtents({{"@0-B0@", 0.0, 2.0}, {"@1-B0@", 2.0, 4.0}, {"@2-B0@", 4.0, 6.0}});
	std::vector<BlockingTimeDiagramSegment> same = {envelope("@2-B0@", 4.0, 6.0)};
	matchBlockingSegmentsToSections(same, route, "route0");
	ok &= expect(same[0].referenceSection == 3
			&& same[0].referenceNote == "Reference section: 3 of 3, @2-B0@, 4.000 to 6.000 km (same section)",
		"same id and interval gives the section and its note");

	std::vector<BlockingTimeDiagramSegment> opposite = {envelope("@1-B0@", 4.0, 2.0)};
	matchBlockingSegmentsToSections(opposite, route, "route0");
	ok &= expect(opposite[0].referenceSection == 2 && opposite[0].referenceNote.find("(same section)") != std::string::npos,
		"an envelope from high X to low X matches the same section");

	std::vector<BlockingTimeDiagramSegment> otherId = {envelope("@9-B0@", 2.0, 4.0)};
	matchBlockingSegmentsToSections(otherId, route, "route0");
	ok &= expect(otherId[0].referenceSection == 0 && otherId[0].referenceNote == "Not a section of the reference route route0",
		"the same interval with another id is not a section of the route");

	std::vector<BlockingTimeDiagramSegment> point = {envelope("@1-B0@", 3.0, 3.0)};
	matchBlockingSegmentsToSections(point, route, "route0");
	ok &= expect(point[0].referenceSection == 0 && point[0].referenceNote.empty(),
		"a zero-length record is a location and stays unevaluated");

	std::vector<BlockingTimeDiagramSegment> none = {envelope("@1-B0@", 2.0, 4.0)};
	matchBlockingSegmentsToSections(none, {}, "route0");
	ok &= expect(none[0].referenceSection == 0 && none[0].referenceNote.empty(), "no sections evaluate nothing");

	const auto twice = buildBlockSectionExtents({{"@x@", 0.0, 1.0}, {"@x@", 5.0, 6.0}});
	std::vector<BlockingTimeDiagramSegment> second = {envelope("@x@", 5.0, 6.0)};
	matchBlockingSegmentsToSections(second, twice, "route0");
	ok &= expect(second[0].referenceSection == 2 && second[0].referenceNote.find("2 of 2") != std::string::npos,
		"a repeated id matches the occurrence with the equal interval");
	std::vector<BlockingTimeDiagramSegment> overlapping = {envelope("@x@", 0.5, 1.5), envelope("@x@", 5.5, 8.0)};
	matchBlockingSegmentsToSections(overlapping, twice, "route0");
	ok &= expect(overlapping[0].referenceSection == 1
			&& overlapping[0].referenceNote
				== "Reference section: 1 of 2, @x@, 0.000 to 1.000 km (shares a block with this envelope's section)"
			&& overlapping[1].referenceSection == 2,
		"an envelope of the same id between the intervals shares a block with the first section it overlaps");

	const auto duplicated = buildBlockSectionExtents({{"@d@", 0.0, 1.0}, {"@d@", 0.0, 1.0}});
	std::vector<BlockingTimeDiagramSegment> first = {envelope("@d@", 0.0, 1.0)};
	matchBlockingSegmentsToSections(first, duplicated, "route0");
	ok &= expect(first[0].referenceSection == 0 && first[0].referenceNote.find("Ambiguous") != std::string::npos
			&& duplicated.size() == 2,
		"coincident occurrences are retained but cannot identify one envelope occurrence");
	std::vector<BlockingTimeDiagramSegment> sharedMany = {envelope("@d@", 0.2, 0.8)};
	matchBlockingSegmentsToSections(sharedMany, duplicated, "route0");
	ok &= expect(sharedMany[0].referenceSection == 0 && sharedMany[0].referenceNote.find("Ambiguous") != std::string::npos,
		"shared-resource overlap with several occurrences is ambiguous");
	same[0].startPositionKm = same[0].endPositionKm;
	matchBlockingSegmentsToSections(same, route, "route0");
	ok &= expect(same[0].referenceSection == 0 && same[0].referenceNote.empty(), "point rematch clears stale section metadata");
	matchBlockingSegmentsToSections(second, {}, "route0");
	ok &= expect(second[0].referenceSection == 0 && second[0].referenceNote.empty(), "empty route rematch clears stale section metadata");

	const auto composite = buildBlockSectionExtents({{"@3-B157@-59.120000/@34-B88@-59.130000", 10.0, 10.5}});
	std::vector<BlockingTimeDiagramSegment> plain = {envelope("@34-B88@", 10.0, 10.5)};
	matchBlockingSegmentsToSections(plain, composite, "route32");
	ok &= expect(plain[0].referenceSection == 1
			&& plain[0].referenceNote
				== "Reference section: 1 of 1, @3-B157@-59.120000/@34-B88@-59.130000, 10.000 to 10.500 km "
				   "(shares a block with this envelope's section)",
		"a plain id matches the composite section that holds its block");
	std::vector<BlockingTimeDiagramSegment> apart = {envelope("@34-B88@", 11.0, 12.0)};
	matchBlockingSegmentsToSections(apart, composite, "route32");
	ok &= expect(apart[0].referenceSection == 0, "a shared block without an overlapping interval is not matched");

	// A section without geometry is never a candidate, even when its ends would be equal.
	const auto unusable = buildBlockSectionExtents({{"@t@", 0.0, 5e-10}});
	std::vector<BlockingTimeDiagramSegment> tiny = {envelope("@t@", 0.0, 5e-10)};
	matchBlockingSegmentsToSections(tiny, unusable, "route0");
	ok &= expect(tiny[0].referenceSection == 0 && tiny[0].referenceNote == "Not a section of the reference route route0",
		"a section without geometry is not a candidate");

	// Rows of the export.
	const std::vector<std::string> header = {"Train", "Block", "Occupation start[s]", "Occupation end[s]", "Position[km]",
		"Segment type", "Planned reference", "Station", "Planned time[s]", "Start X[km]", "End X[km]", "Original start[s]",
		"Original end[s]", "Approach[s]", "Run start[s]", "Run end[s]", "Clearance[s]", "Setup[s]", "Sight reaction[s]",
		"Release[s]", "Run margin[s]", "Layer", "Reference section"};
	ok &= expect(blockingTimeCsvHeader() == header, "the export header has the 23 columns in order");

	const auto exported = buildBlockSectionExtents({{"@0-B0@", 0.0, 2.0}, {"@bad@", 2.0, nan}, {"@fall@", 0.5, 0.23}});
	const auto rows = blockSectionCsvRows(exported);
	ok &= expect(rows.size() == 3 && rows[0].size() == header.size() && rows[1].size() == header.size()
			&& rows[2].size() == header.size(),
		"every section row is as wide as the header");
	if (rows.size() == 3 && rows[0].size() == header.size() && rows[1].size() == header.size()
		&& rows[2].size() == header.size()) {
		ok &= expect(rows[0][1] == "@0-B0@" && rows[0][4] == "1" && rows[0][5] == "block section" && rows[0][9] == "0"
				&& rows[0][10] == "2" && rows[0][21] == "block section extent" && rows[0][22] == "1",
			"a section row holds its id, midpoint, ends, layer and order");
		bool emptyElsewhere = true;
		for (std::size_t column = 0; column < header.size(); ++column)
			if (column != 1 && column != 4 && column != 5 && column != 9 && column != 10 && column != 21 && column != 22)
				emptyElsewhere = emptyElsewhere && rows[0][column].empty() && rows[1][column].empty()
					&& rows[2][column].empty();
		ok &= expect(emptyElsewhere, "a section row leaves train, times and components empty");
		ok &= expect(rows[1][1] == "@bad@" && rows[1][4].empty() && rows[1][9].empty() && rows[1][10].empty()
				&& rows[1][5] == "block section" && rows[1][21] == "block section extent (geometry missing)"
				&& rows[1][22] == "2",
			"a section without geometry has no coordinates and its own layer");
		ok &= expect(rows[2][9] == csv::formatDouble(0.5) && rows[2][10] == csv::formatDouble(0.23)
				&& rows[2][4] == csv::formatDouble((0.5 + 0.23) / 2.0) && rows[2][22] == "3",
			"a falling section is written as the route holds it");
	}
	ok &= expect(blockSectionCsvRows({}).empty(), "no sections give no rows");

	// Note of the sections without geometry.
	const auto sections = [](int count, int invalid) {
		std::vector<BlockSectionInput> inputs;
		for (int i = 0; i < count; ++i)
			inputs.push_back({"@s" + std::to_string(i) + "@", static_cast<double>(i), i < invalid ? static_cast<double>(i) : i + 1.0});
		return buildBlockSectionExtents(inputs);
	};
	ok &= expect(blockSectionGeometryNote(sections(4, 0)).empty() && blockSectionGeometryNote({}).empty(),
		"usable extents give no note");
	ok &= expect(blockSectionGeometryNote(sections(4, 1)) == " Reference sections without a usable extent: @s0@.",
		"one section without geometry is named");
	ok &= expect(blockSectionGeometryNote(sections(4, 3)) == " Reference sections without a usable extent: @s0@, @s1@, @s2@.",
		"three sections without geometry are all named");
	ok &= expect(blockSectionGeometryNote(sections(5, 4))
			== " Reference sections without a usable extent: @s0@, @s1@, @s2@ and 1 more.",
		"more than three sections without geometry end with the number left out");
	const auto mixed = buildBlockSectionExtents({{"@a@", 0.0, 1.0}, {"@b@", 1.0, 1.0}, {"@c@", 1.0, 2.0}, {"@d@", 2.0, nan}});
	ok &= expect(blockSectionGeometryNote(mixed) == " Reference sections without a usable extent: @b@, @d@.",
		"the note lists the sections without geometry in route order and no others");
	const auto returning = buildBlockSectionExtents({{"@a@", 2, 8}, {"@b@", 8, -2}, {"@a@", -2, 2}});
	ok &= expect(blockSectionBounds(returning) == std::make_optional(std::make_pair(-2.0, 8.0)),
		"returning route includes intermediate endpoints, not only first/last");
	const auto selectedRepeat = scopedBlockSectionExtents(returning, {2});
	ok &= expect(selectedRepeat.size() == 1 && selectedRepeat[0].order == 3 && selectedRepeat[0].startKm == -2,
		"scope selects repeated occurrence by index and retains original order");
	ok &= expect(!blockSectionBounds({broken[1], broken[3]}),
		"missing geometry yields no invented coordinate bounds");
	ok &= expect(!blockSectionCapacityScopeIssue(returning, {0}).empty(), "capacity rejects a selected repeated resource with another occurrence outside scope");
	ok &= expect(!blockSectionCapacityScopeIssue(returning, {0, 1, 2}).empty(), "capacity rejects repeated selected entry resource");
	const auto unrelatedRepeat = buildBlockSectionExtents({{"@entry@", 0, 1}, {"@else@", 1, 2}, {"@else@", 2, 3}});
	ok &= expect(blockSectionCapacityScopeIssue(unrelatedRepeat, {0}).empty(), "unrelated repeats do not block a representable capacity scope");
	const auto sharedScope = buildBlockSectionExtents({{"@a@/@b@", 0, 1}, {"@b@", 2, 3}});
	ok &= expect(!blockSectionCapacityScopeIssue(sharedScope, {0}).empty(), "capacity rejects shared composite resource outside selected scope");
	auto entry = block("@entry@", 1, 2, 0, 1000, "None", "None", true);
	entry.startRunTime = 1;
	auto duplicateEntry = entry;
	auto incompleteEntry = entry;
	incompleteEntry.isComplete = false;
	auto unrelated = entry;
	unrelated.blockId = "@other@";
	ok &= expect(blockingTimeEntryAmbiguous({entry, duplicateEntry}, "@entry@"),
		"several valid profile entry records refuse an arbitrary first StartRunTime");
	ok &= expect(!blockingTimeEntryAmbiguous({entry, incompleteEntry, unrelated, unrelated}, "@entry@"),
		"incomplete entry and unrelated repeated profile records do not cause refusal");
	return ok;
}

int main() {
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
	std::vector<BlockingTimeDiagramSegment> segments = buildBlockingTimeDiagramSegments(trains, names);

	bool ok = true;
	ok &= expect(!shareBlockingTimeResource("2-B0", "2-B4"),
		"distinct decorated block resources do not match");
	ok &= expect(shareBlockingTimeResource("@2-B0@-1.0/@3-B1@-2.0", "2-B0"),
		"composite block matches its exact component");
	ok &= expect(segments.size() == 3, "invalid and incomplete intervals are filtered");
	if (segments.size() >= 3) {
		ok &= expect(segments[0].trainName == "A", "first segment train name");
		ok &= expect(segments[0].style == BlockingTimeSegmentStyle::Critical, "overlapping matching blocks are critical");
		ok &= expect(segments[0].startTime == 10.0, "first segment start time");
		ok &= expect(segments[0].endTime == 20.0, "first segment end time");
		ok &= expect(segments[0].midPositionKm == 1.2, "first segment midpoint");
		ok &= expect(segments[1].trainName == "A", "switch segment train name");
		ok &= expect(segments[1].style == BlockingTimeSegmentStyle::Switch, "switch segment style");
		ok &= expect(segments[1].startPositionKm == 1.6 && segments[1].endPositionKm == 2.2,
			"directed rectangle endpoints retain their chart coordinates");
		ok &= expect(segments[2].trainName == "B", "critical station train name");
		ok &= expect(segments[2].style == BlockingTimeSegmentStyle::CriticalStation, "critical station segment style");
		ok &= expect(segments[2].midPositionKm == 1.25, "critical station midpoint");
	}

	const std::vector<BlockingTimeDiagramSegment> scoped = filterBlockingTimeDiagramSegments(
		segments, {"A"}, {"@12-B0@"}, 12.0, 18.0);
	ok &= expect(scoped.size() == 1, "route block and train scope filters segments");
	if (!scoped.empty()) {
		ok &= expect(scoped[0].startTime == 12.0 && scoped[0].endTime == 18.0
				&& scoped[0].originalStartTime == 10.0 && scoped[0].originalEndTime == 20.0,
			"time scope clips displayed bounds but retains original evidence");
		ok &= expect(scoped[0].style == BlockingTimeSegmentStyle::Critical,
			"critical style survives when the conflicting train is hidden");
	}

	const std::vector<BlockingTimePlannedReference> references = {
		{"A", "Origin", "departure", 10.0, 0.0},
		{"A", "Destination", "arrival", 30.0, 2.0},
		{"B", "Other", "departure", 40.0, 0.0}};
	const auto crossing = filterBlockingTimePlannedReferences(references, 15.0, 25.0);
	ok &= expect(crossing.size() == 2 && crossing.front().time == 10.0 && crossing.back().time == 30.0,
		"planned scope retains both source points for a visible line crossing");
	const auto plotted = clipBlockingTimePlannedReferences(references, 15.0, 25.0);
	ok &= expect(plotted.size() == 1 && plotted[0].size() == 2
			&& plotted[0][0].time == 15.0 && plotted[0][1].time == 25.0
			&& plotted[0][0].positionKm == 0.5 && plotted[0][1].positionKm == 1.5
			&& plotted[0][0].stationName.empty() && plotted[0][1].stationName.empty()
			&& plotted[0][0].eventType == "clipped planned interpolation",
		"selected window clips rendered planned line without presenting boundary as station events");
	const auto gaps = clipBlockingTimePlannedReferences({references[0],
															{"A", "", "", std::numeric_limits<double>::quiet_NaN(), 0.0}, references[1]},
		15.0, 25.0);
	ok &= expect(gaps.empty(), "omitted planned events cannot be bridged by a rendered line");
	ok &= expect(filterBlockingTimePlannedReferences(references, 31.0, 39.0).empty(),
		"off-window planned points do not create an empty scoped chart or export");
	const auto nearTouch = buildBlockingTimeDiagramSegments({{block("T", 0.0, 10.00000005, 0.0, 100.0, "None", "None", true)},
																{block("T", 10.0, 20.0, 0.0, 100.0, "None", "None", true)}},
		{"A", "B"});
	ok &= expect(nearTouch.size() == 2
			&& nearTouch[0].style == BlockingTimeSegmentStyle::Default
			&& nearTouch[1].style == BlockingTimeSegmentStyle::Default,
		"sub-tolerance touching is not also styled as an overlap conflict");

	const auto reversed = buildBlockingTimeDiagramSegments({{block("reverse", 1, 2, 2000, 1000, "None", "None", true),
															   block("point", 2, 3, 1000, 1000, "None", "None", true)}},
		{"R"});
	ok &= expect(reversed.size() == 2 && reversed[0].startPositionKm == 2.0
			&& reversed[0].endPositionKm == 1.0 && reversed[1].startPositionKm == reversed[1].endPositionKm,
		"reversed and zero-width boundaries preserve exact directed positions");
	auto missing = block("missing clearance", 1, 2, 0, 100, "None", "None", true);
	missing.endClearTime = -1;
	auto nonfinite = block("nonfinite", 1, 2, 0, 100, "None", "None", true);
	nonfinite.posEnd = std::numeric_limits<double>::infinity();
	auto nonfiniteClearance = block("nonfinite clearance", 1, 2, 0, 100, "None", "None", true);
	nonfiniteClearance.endClearTime = std::numeric_limits<double>::infinity();
	ok &= expect(buildBlockingTimeDiagramSegments({{missing, nonfinite, nonfiniteClearance}}, {"R"}).empty(),
		"missing clearance and nonfinite geometry cannot be rendered as measured envelopes");
	ok &= blockSectionTests();
	if (!ok)
		return 1;

	std::cout << "all BlockingTimeDiagram tests passed\n";
	return 0;
}
