#ifndef BLOCKINGTIMEDIAGRAM_H
#define BLOCKINGTIMEDIAGRAM_H

#include <algorithm>
#include <limits>
#include <string>
#include <optional>
#include <utility>
#include <vector>

struct SceneSectionLabels;

constexpr double kBlockingTimeToleranceSeconds = 1e-7;

enum class BlockingTimeSegmentStyle {
	Default,
	Station,
	Switch,
	SwitchStation,
	Critical,
	CriticalStation
};

struct BlockingTimeDiagramInput {
	std::string blockId;
	double startOccTime = -1.0;
	double endOccTime = -1.0;
	double posStart = -1.0;
	double posEnd = -1.0;
	std::string switchName = "None";
	std::string stationName = "None";
	bool isComplete = false;
	bool capacityCritical = false;
	// Optional measured/calculated components; negative is the runtime sentinel.
	double startApproachTime = -1.0, startRunTime = -1.0;
	double endRunTime = -1.0, endClearTime = -1.0;
	double setupTime = -1.0, sightReactionTime = -1.0;
	double releaseTime = -1.0, runTimeMargin = -1.0;
};

struct BlockingTimeDiagramSegment {
	std::string trainName;
	std::string blockId;
	double startTime = 0.0;
	double endTime = 0.0;
	double midPositionKm = 0.0;
	double startPositionKm = 0.0;
	double endPositionKm = 0.0;
	double originalStartTime = 0.0;
	double originalEndTime = 0.0;
	double startApproachTime = -1.0, startRunTime = -1.0;
	double endRunTime = -1.0, endClearTime = -1.0;
	double setupTime = -1.0, sightReactionTime = -1.0;
	double releaseTime = -1.0, runTimeMargin = -1.0;
	BlockingTimeSegmentStyle style = BlockingTimeSegmentStyle::Default;
	bool capacityCritical = false;
	// Order of the matched section of the reference route, 0 for none.
	int referenceSection = 0;
	// What the hover says about the section of the reference route; empty when the block is that section, or when it
	// was not matched to the sections.
	std::string referenceNote;
};

struct CapacityCompressionRow;

// Undo the occupation's profile-based displacement, not the timetable shift.
void restoreCompressedOriginalTimes(std::vector<BlockingTimeDiagramSegment>& segments,
	const std::vector<CapacityCompressionRow>& compression);

struct BlockingTimePlannedReference {
	std::string trainName;
	std::string stationName;
	std::string eventType;
	double time = 0.0;
	double positionKm = 0.0;
};

std::vector<BlockingTimeDiagramSegment> buildBlockingTimeDiagramSegments(
	const std::vector<std::vector<BlockingTimeDiagramInput>>& trains,
	const std::vector<std::string>& trainNames);

// Shared resource matcher used by both diagram conflict classification and
// capacity analysis. It matches exact plain and decorated/composite components.
bool shareBlockingTimeResource(const std::string& firstBlockId, const std::string& secondBlockId);
bool shareBlockingTimeResource(const BlockingTimeDiagramInput& first,
	const BlockingTimeDiagramInput& second);
bool validBlockingTimeDiagramInput(const BlockingTimeDiagramInput& input);

// Return copies of already-classified occupation segments in the selected
// train/block/time scope. Empty train or block lists mean no restriction.
std::vector<BlockingTimeDiagramSegment> filterBlockingTimeDiagramSegments(
	const std::vector<BlockingTimeDiagramSegment>& segments,
	const std::vector<std::string>& allowedTrainIds,
	const std::vector<std::string>& allowedBlockIds,
	double startTime,
	double endTime);

// Keep planned source points that fall in the time window or form a visible
// line segment across it. Input points for each train must be contiguous.
std::vector<BlockingTimePlannedReference> filterBlockingTimePlannedReferences(
	const std::vector<BlockingTimePlannedReference>& references,
	double startTime,
	double endTime);

// Each returned group is a standalone event or one clipped adjacent event pair.
// An empty station name marks an interpolated boundary, never a station event.
std::vector<std::vector<BlockingTimePlannedReference>> clipBlockingTimePlannedReferences(
	const std::vector<BlockingTimePlannedReference>& references, double startTime, double endTime);

// One section of a route as the route holds it, in km on the runtime route coordinate.
struct BlockSectionInput {
	std::string id;
	double startKm = 0.0;
	double endKm = 0.0;
	double geoStartKm = std::numeric_limits<double>::quiet_NaN(); ///< Geo X of the start in km, the scene X; NaN when unknown
	double geoEndKm = std::numeric_limits<double>::quiet_NaN();	  ///< Geo X of the end in km, the scene X; NaN when unknown
	bool isSwitch = false;										  ///< The section holds a diverging switch
};

// A section of the reference route with the ends exactly as the route holds them. The end can lie
// below the start; the extent is never swapped. Without a usable geometry `issue` says why.
struct BlockSectionExtent {
	int order = 0; // 1-based position in the route
	std::string id;
	double startKm = 0.0;
	double endKm = 0.0;
	bool geometryValid = false;
	std::string issue;
	/// Geo X of the start in km. The route distance when the input lacks the geo X of an end, so NaN only when the route distance is NaN.
	double geoStartKm = std::numeric_limits<double>::quiet_NaN();
	double geoEndKm = std::numeric_limits<double>::quiet_NaN(); ///< Geo X of the end in km, as geoStartKm
	bool isSwitch = false;										///< The section holds a diverging switch

	double lowerKm() const { return std::min(startKm, endKm); }
	double upperKm() const { return std::max(startKm, endKm); }
};

// One extent per input in input order. Nothing is sorted, merged, shifted or invented; a section
// whose coordinates are not finite or that has no length is listed without geometry. A section that lacks the geo X of
// an end takes its route distance as geo X at both ends.
std::vector<BlockSectionExtent> buildBlockSectionExtents(const std::vector<BlockSectionInput>& inputs);

// Exact id/interval matches take precedence over overlapping shared resources. Several candidates
// are ambiguous, never first-match. Every call clears stale fields, including locations/empty routes.
// An exact match sets the section and no note. The notes name a section by its label and id when `labels` is given,
// else by its id.
void matchBlockingSegmentsToSections(std::vector<BlockingTimeDiagramSegment>& segments,
	const std::vector<BlockSectionExtent>& extents, const std::string& referenceRouteId,
	const SceneSectionLabels* labels = nullptr);

// Scope uses zero-based route indices, not ids; selected extents keep their original route order.
std::vector<BlockSectionExtent> scopedBlockSectionExtents(const std::vector<BlockSectionExtent>& extents,
	const std::vector<int>& indices);
std::optional<std::pair<double, double>> blockSectionBounds(const std::vector<BlockSectionExtent>& extents);
// Zero-based route indices from `first` to `last` (either order) that exist in a route of `count` sections.
std::vector<int> blockSectionRangeIndices(int first, int last, int count);
// An envelope belongs to the scope when it shares a resource with a section that has geometry and lies on its interval
// (a zero-length envelope is a location). A repeated id selects only the occurrence whose interval holds the envelope.
bool blockingSegmentInSections(const BlockingTimeDiagramSegment& segment, const std::vector<BlockSectionExtent>& sections);
// Whether a position on the reference route lies on the interval of a section that has geometry.
bool positionInBlockSections(double positionKm, const std::vector<BlockSectionExtent>& sections);
// The existing capacity API selects resources by id and needs a unique entry occurrence.
std::string blockSectionCapacityScopeIssue(const std::vector<BlockSectionExtent>& extents,
	const std::vector<int>& indices);

// A resource-id entry is unusable when several valid profile records could provide it.
bool blockingTimeEntryAmbiguous(const std::vector<BlockingTimeDiagramInput>& occupations,
	const std::string& entryBlockId);

// Names of the columns of the blocking-time export, in order. A compression never moves the times named "Original", and
// the last three columns give the positions as geo X through the PositionScale of the sections of the export.
const std::vector<std::string>& blockingTimeCsvHeader();

inline constexpr char kBlockSectionSegmentType[] = "block section";
inline constexpr char kBlockSectionLayer[] = "block section extent";
inline constexpr char kBlockSectionMissingLayer[] = "block section extent (geometry missing)";
inline constexpr char kUnmappedEnvelopeLayer[] = "unmapped blocking envelope";

// One row of the blocking-time export for each extent, as wide as the header.
std::vector<std::vector<std::string>> blockSectionCsvRows(const std::vector<BlockSectionExtent>& extents);

// The notice of the section strip that lists the sections without a usable extent; empty when every extent is usable.
// A section is named by its label and id when `labels` is given.
std::string blockSectionGeometryNote(const std::vector<BlockSectionExtent>& extents, const SceneSectionLabels* labels = nullptr);

// The note above a blocking-time chart: what it leaves out, from the parts that apply. `omitted` blocks or stops could
// not be placed on the route `routeId`; `nothingPlotted` adds that the chart is empty, which `unplaced` blames on blocks
// that could not be placed. Empty when nothing is missing.
std::string blockingTimeNote(int omitted, bool nothingPlotted, bool unplaced, const std::string& routeId);

#endif // BLOCKINGTIMEDIAGRAM_H
