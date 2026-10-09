// Aspects of the mixed signalling on routes built by hand.
//
//   test_signalling_aspects --expect FILE
//
// The test puts a route of up to seven sections in the global route list, without a scene, and runs the release and
// the activation of the mixed signalling on it in the order of a step of the simulation. After a step it writes the
// code, state, signal speed limit, speed in braking and exit speed of every section as one line of text. The lines
// are compared with the golden file FILE. The speed limits of the sections are numbers of this test, not railway data.
//
// EGTRAIN_UPDATE_EXPECTATIONS=1 writes FILE instead. Two kinds of check do not read FILE and run first: the aspects
// of one step for every level, written out below, and the two lists of occupied and connected sections, which a step
// must leave as they were. When one of them fails, nothing is written.

#include "scene/SignallingLevel.h"
#include "simulation/Signalling.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <list>
#include <locale>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

Logger owl;

namespace {

constexpr int kNoLevel = kSignallingLevelUnset;
constexpr int kAtb = levelValue(SignallingLevel::Atb);
constexpr int kEtcsL1 = levelValue(SignallingLevel::EtcsLevel1);
constexpr int kEtcsL2 = levelValue(SignallingLevel::EtcsLevel2);
constexpr int kEtcsL3 = levelValue(SignallingLevel::EtcsLevel3);
constexpr int kVirtualCoupling = levelValue(SignallingLevel::VirtualCoupling);
constexpr int kBacc = levelValue(SignallingLevel::Bacc);
constexpr int kAllLevels[] = {kNoLevel, kAtb, kEtcsL1, kEtcsL2, kEtcsL3, kVirtualCoupling, kBacc};

constexpr std::size_t kLongRoute = 7;
constexpr std::size_t kShortRoute = 6;
// Speed limit of the first arc of each section in m/s; each further arc is kArcSpeedStep lower. These are numbers
// of the test, not railway data. Section 3 is below the 11.111 m/s that the routines of levels 0 and 5 write.
constexpr double kSectionSpeed[kLongRoute] = {36.0, 33.0, 30.0, 8.0, 27.0, 24.0, 21.0};
constexpr double kArcSpeedStep = 2.0;
// What a section holds before a step when no routine has written it. The constructors give code 270, state green,
// signal speed limit 999 and exit speed 0.
constexpr double kDirtyCode = 11.0;
constexpr const char* kDirtyState = "?";
constexpr double kDirtySignalSpeed = 7.0;
constexpr double kDirtyExitSpeed = 5.0;
// Signal speeds of group V.
constexpr double kOtherSignalCode1 = 14.5;
constexpr double kOtherSignalCode2 = 3.5;
constexpr std::size_t kMaxShown = 20;
constexpr const char* kActualFile = "signalling-aspects.actual.txt";

using Levels = std::vector<int>;
using Indices = std::vector<std::size_t>;
using Ids = std::list<std::string>;

// Arcs per section: 1 1 1 ..., 2 1 2 1 ..., 1 2 1 2 ...
enum class Shape {
	One,
	TwoOne,
	OneTwo
};
enum class Start {
	Clean,
	Dirty
};

struct RouteSpec {
	Levels levels;
	Shape shape = Shape::One;
	Start start = Start::Dirty;
	bool reversedIds = false; // the ids run s<n-1> to s0
	bool reversedDirection = false;
};

// The lines of the golden file and the checks that failed while they were made.
struct Report {
	std::vector<std::string> rows; // comment lines, which start with '#', and case lines
	bool ok = true;
	std::size_t failed = 0;

	void comment(const std::string& text) {
		rows.push_back("# " + text);
	}
	void line(const std::string& text) {
		rows.push_back(text);
	}
	void expect(bool condition, const std::string& message) {
		if (condition)
			return;
		ok = false;
		if (++failed <= kMaxShown)
			std::cerr << "failed: " << message << "\n";
	}
};

// ---------------------------------------------------------------------------
// The globals of the simulation. Nothing else in this file names them.
// ---------------------------------------------------------------------------

// Takes the globals that the test sets and puts them back at the end.
class SavedGlobals {
public:
	SavedGlobals() {
		train_route.clear();
	}
	~SavedGlobals() {
		train_route = std::move(routes_);
		N_Routes = routeCount_;
		BlocksOccupied = occupied_;
		BlocksConnected = connected_;
		signalCode1 = signalCode1_;
		signalCode2 = signalCode2_;
		singleTrackLimits = limits_;
		resetSingleTrackLocks();
		singleTrackHeld = held_;
	}
	SavedGlobals(const SavedGlobals&) = delete;
	SavedGlobals& operator=(const SavedGlobals&) = delete;

private:
	// The members take the values when the object is made; the route list is emptied for the test.
	std::vector<Route> routes_ = std::move(train_route);
	int routeCount_ = N_Routes;
	Ids occupied_ = BlocksOccupied;
	Ids connected_ = BlocksConnected;
	double signalCode1_ = signalCode1;
	double signalCode2_ = signalCode2;
	std::vector<std::tuple<std::string, std::string, std::string, std::string>> limits_ = singleTrackLimits;
	std::vector<int> held_ = singleTrackHeld;
};

void setRoutes(std::vector<Route>&& routes) {
	train_route = std::move(routes);
	N_Routes = static_cast<int>(train_route.size());
}

const std::vector<Section>& sectionsOf(std::size_t route) {
	return train_route[route].sequence_of_block_sections;
}

std::pair<double, double> signalSpeeds() {
	return {signalCode1, signalCode2};
}

void setSignalSpeeds(const std::pair<double, double>& speeds) {
	signalCode1 = speeds.first;
	signalCode2 = speeds.second;
}

// Single-track limit between s1 and s4, protected by s2 and s3, held in the direction held (1, -1 or 0).
void holdSingleTrack(int held) {
	singleTrackLimits.clear();
	singleTrackLimits.emplace_back("s2", "s3", "s1", "s4");
	resetSingleTrackLocks();
	singleTrackHeld = {held};
}

void freeSingleTrack() {
	singleTrackLimits.clear();
	resetSingleTrackLocks();
}

// A step as the simulation makes it once the trains have been placed: release, then activation. Returns whether both
// lists are as they were set after each of the two calls.
bool runStep(const Ids& occupied, const Ids& connected) {
	BlocksOccupied = occupied;
	BlocksConnected = connected;
	releaseMixedSignallingSystem();
	const bool unchangedByRelease = BlocksOccupied == occupied && BlocksConnected == connected;
	activateMixedSignallingSystem();
	const bool unchangedByActivation = BlocksOccupied == occupied && BlocksConnected == connected;
	BlocksOccupied.clear();
	BlocksConnected.clear();
	return unchangedByRelease && unchangedByActivation;
}

// Only the release. Returns whether both lists are as they were set.
bool runRelease(const Ids& connected) {
	BlocksOccupied.clear();
	BlocksConnected = connected;
	releaseMixedSignallingSystem();
	const bool unchanged = BlocksOccupied.empty() && BlocksConnected == connected;
	BlocksConnected.clear();
	return unchanged;
}

// The last train leaves the route at section id. Returns what the call puts in BlocksConnected.
Ids runLastSection(const std::string& id) {
	BlocksOccupied.clear();
	BlocksConnected.clear();
	relLastSectionMixedSignalling(id);
	Ids connected = BlocksConnected;
	BlocksConnected.clear();
	return connected;
}

// ---------------------------------------------------------------------------
// Routes
// ---------------------------------------------------------------------------

std::string idOf(std::size_t index) {
	return "s" + std::to_string(index);
}

int arcCount(Shape shape, std::size_t index) {
	switch (shape) {
		case Shape::One:
			return 1;
		case Shape::TwoOne:
			return index % 2 == 0 ? 2 : 1;
		case Shape::OneTwo:
			return index % 2 == 0 ? 1 : 2;
	}
	return 1;
}

// The sections are in a vector of exactly the size of the route, so that a sanitizer reports an access before the
// first or after the last section.
Route makeRoute(const RouteSpec& spec) {
	const std::size_t count = spec.levels.size();
	const bool dirty = spec.start == Start::Dirty;
	Route route;
	route.N_Block_Sections = static_cast<int>(count);
	route.reversed_direction = spec.reversedDirection;
	route.sequence_of_block_sections = std::vector<Section>(count);
	for (std::size_t i = 0; i < count; ++i) {
		Section& section = route.sequence_of_block_sections[i];
		section.ID = idOf(spec.reversedIds ? count - 1 - i : i);
		section.SignallingLevel = spec.levels[i];
		section.total_arcs = arcCount(spec.shape, i);
		if (dirty) {
			section.code = kDirtyCode;
			std::snprintf(section.state, sizeof section.state, "%s", kDirtyState);
			section.exit_speed = kDirtyExitSpeed;
		}
		for (int a = 0; a < section.total_arcs; ++a) {
			Arc& arc = section.arcs_in_signalling_block_section[a];
			arc.speedLimit = kSectionSpeed[i] - kArcSpeedStep * a;
			if (dirty)
				arc.signalSpeedLimit = kDirtySignalSpeed;
		}
	}
	return route;
}

void installRoutes(const std::vector<RouteSpec>& specs) {
	std::vector<Route> routes;
	routes.reserve(specs.size());
	for (const RouteSpec& spec : specs)
		routes.push_back(makeRoute(spec));
	setRoutes(std::move(routes));
}

RouteSpec routeSpec(Levels levels, Shape shape = Shape::One, Start start = Start::Dirty) {
	RouteSpec spec;
	spec.levels = std::move(levels);
	spec.shape = shape;
	spec.start = start;
	return spec;
}

Levels uniform(int level, std::size_t count) {
	return Levels(count, level);
}

// The first border sections have level west and the others level east.
Levels twoLevels(int west, int east, std::size_t border, std::size_t count) {
	Levels levels(count, east);
	std::fill(levels.begin(), levels.begin() + static_cast<std::ptrdiff_t>(border), west);
	return levels;
}

Ids idsOf(const Indices& indices) {
	Ids ids;
	for (const std::size_t index : indices)
		ids.push_back(idOf(index));
	return ids;
}

// All sets of at most maxSize of the indices 0 to count - 1: by size, and within a size in the order of the bit
// patterns.
std::vector<Indices> occupiedSets(std::size_t count, std::size_t maxSize) {
	std::vector<Indices> sets;
	for (std::size_t size = 0; size <= maxSize; ++size) {
		for (unsigned mask = 0; mask < (1u << count); ++mask) {
			Indices set;
			for (std::size_t i = 0; i < count; ++i)
				if (mask & (1u << i))
					set.push_back(i);
			if (set.size() == size)
				sets.push_back(set);
		}
	}
	return sets;
}

// ---------------------------------------------------------------------------
// Text
// ---------------------------------------------------------------------------

std::string numberText(double value) {
	std::ostringstream stream;
	stream.imbue(std::locale::classic());
	stream.precision(9);
	stream << value;
	return stream.str();
}

std::string stateText(const char* state) {
	const std::string text(state);
	if (text == "green")
		return "G";
	if (text == "yellow")
		return "Y";
	if (text == "red")
		return "R";
	if (text == "red_red")
		return "RR";
	return text;
}

std::string joinedText(const std::vector<double>& values) {
	std::string text;
	for (const double value : values)
		text += (text.empty() ? "" : "/") + numberText(value);
	return text;
}

// The signal speed limit of the arcs, once when they are all the same.
std::string signalText(const Section& section) {
	std::vector<double> values;
	for (int a = 0; a < section.total_arcs; ++a)
		values.push_back(section.arcs_in_signalling_block_section[a].signalSpeedLimit);
	if (std::all_of(values.begin(), values.end(), [&values](double value) { return value == values.front(); }))
		values.resize(1);
	return joinedText(values);
}

std::string brakingText(const Section& section) {
	std::vector<double> values;
	for (int a = 0; a < section.total_arcs; ++a)
		values.push_back(section.arcs_in_signalling_block_section[a].speedInBraking);
	return joinedText(values);
}

std::string aspectText(const Section& section) {
	return numberText(section.code) + ":" + stateText(section.state) + ":" + signalText(section);
}

std::string tokenText(const Section& section) {
	return aspectText(section) + ":" + brakingText(section) + ":" + numberText(section.exit_speed);
}

std::string sectionsText(const std::vector<Section>& sections, std::string (*text)(const Section&)) {
	std::string joined;
	for (const Section& section : sections)
		joined += (joined.empty() ? "" : " ") + text(section);
	return joined;
}

// The sections of the first routeCount routes, tokens of one route after the other.
std::string routesText(std::size_t routeCount) {
	std::string joined;
	for (std::size_t r = 0; r < routeCount; ++r)
		joined += (joined.empty() ? "" : " || ") + sectionsText(sectionsOf(r), tokenText);
	return joined;
}

std::string levelsText(const Levels& levels) {
	std::string text;
	for (const int level : levels)
		text += level == kNoLevel ? 'u' : static_cast<char>('0' + level);
	return text;
}

std::string shapeText(Shape shape) {
	switch (shape) {
		case Shape::One:
			return "1";
		case Shape::TwoOne:
			return "21";
		case Shape::OneTwo:
			return "12";
	}
	return "1";
}

std::string indicesText(const Indices& indices) {
	std::string text;
	for (const std::size_t index : indices)
		text += (text.empty() ? "" : ",") + std::to_string(index);
	return text.empty() ? "-" : text;
}

std::string idsText(const Ids& ids) {
	std::string text;
	for (const std::string& id : ids)
		text += (text.empty() ? "" : ",") + id;
	return text.empty() ? "-" : text;
}

// "<group> levels=<levels of each route> shape=<shape of the first>", the start of the parameters of a line.
std::string labelOf(const char* group, const std::vector<RouteSpec>& specs) {
	std::string levels;
	for (const RouteSpec& spec : specs)
		levels += (levels.empty() ? "" : "/") + levelsText(spec.levels);
	return std::string(group) + " levels=" + levels + " shape=" + shapeText(specs.front().shape);
}

// ---------------------------------------------------------------------------
// Anchors: the aspects of one step, written out here and derived from the routines, not read from the golden file.
// ---------------------------------------------------------------------------

// One step on a route of seven sections with section 5 occupied.
std::string anchorStep(Report& report, const RouteSpec& spec, const std::string& name) {
	installRoutes({spec});
	report.expect(runStep(idsOf({5}), Ids()), "the lists changed in the step of " + name);
	return sectionsText(sectionsOf(0), aspectText);
}

void checkAnchors(Report& report) {
	struct Anchor {
		const char* name;
		int level;
		const char* aspects;
	};
	const Anchor anchors[] = {
		{"no level", kNoLevel, "270:G:999 270:G:999 270:G:999 270:G:999 270:G:999 270:G:999 270:G:999"},
		{"level 0", kAtb, "270:G:999 270:G:999 270:G:999 180:Y:999 75:R:11.111 0:G:999 270:G:999"},
		{"level 1", kEtcsL1, "270:G:999 270:G:999 270:G:999 180:Y:999 75:R:999 0:G:999 270:G:999"},
		{"level 2", kEtcsL2, "270:G:999 270:G:999 270:G:999 180:Y:999 75:R:999 0:G:999 270:G:999"},
		{"level 3", kEtcsL3, "270:G:999 270:G:999 270:G:999 270:G:999 270:G:999 0:G:999 270:G:999"},
		{"level 4", kVirtualCoupling, "270:G:999 270:G:999 270:G:999 270:G:999 270:G:999 0:G:999 270:G:999"},
		{"level 5", kBacc, "270:G:999 270:G:999 180:Y:999 75:R:11.111 751:RR:0 0:G:999 270:G:999"},
	};
	for (const Anchor& anchor : anchors) {
		const std::string name = std::string("anchor ") + anchor.name;
		const std::string actual = anchorStep(report, routeSpec(uniform(anchor.level, kLongRoute), Shape::One, Start::Clean), name);
		report.expect(actual == anchor.aspects, name + ": expected " + anchor.aspects + ", actual " + actual);
	}

	// All five fields, for the levels 0 and 5.
	const std::pair<int, const char*> full[] = {
		{kAtb, "270:G:999:33:0 270:G:999:30:0 270:G:999:8:0 180:Y:999:11.111:0 75:R:11.111:0:0 0:G:999:21:21 270:G:999:21:0"},
		{kBacc, "270:G:999:33:0 270:G:999:30:0 180:Y:999:8:0 75:R:11.111:0:0 751:RR:0:0:0 0:G:999:21:21 270:G:999:21:0"},
	};
	for (const auto& [level, tokens] : full) {
		const std::string name = "anchor level " + levelsText({level}) + " with all fields";
		anchorStep(report, routeSpec(uniform(level, kLongRoute), Shape::One, Start::Clean), name);
		const std::string actual = sectionsText(sectionsOf(0), tokenText);
		report.expect(actual == tokens, name + ": expected " + tokens + ", actual " + actual);
	}

	// No routine writes a section without a level, so its code, state, signal speed limit and exit speed stay as they were.
	installRoutes({routeSpec(uniform(kNoLevel, kLongRoute))});
	report.expect(runStep(idsOf({3}), Ids()), "the lists changed in the step of the anchor without a level and dirty start");
	for (const Section& section : sectionsOf(0))
		report.expect(aspectText(section) == "11:?:7" && numberText(section.exit_speed) == "5",
			"anchor without a level and dirty start: section " + section.ID + " is " + tokenText(section));
}

// ---------------------------------------------------------------------------
// Groups
// ---------------------------------------------------------------------------

// A step on one route with the sections in occupied in BlocksOccupied.
void stepLine(Report& report, const char* group, const RouteSpec& spec, const Indices& occupied, const std::string& extra = "") {
	const std::string label = labelOf(group, {spec}) + " occ=" + indicesText(occupied) + extra;
	installRoutes({spec});
	report.expect(runStep(idsOf(occupied), Ids()), "the lists changed in the step of " + label);
	report.line(label + " | " + routesText(1));
}

// Uniform levels.
void groupUniform(Report& report) {
	report.comment("U: one level on all sections, one step, dirty start");
	for (const int level : kAllLevels) {
		for (const Indices& occupied : occupiedSets(kLongRoute, 2))
			stepLine(report, "U", routeSpec(uniform(level, kLongRoute)), occupied);
		for (std::size_t count = 1; count <= 3; ++count)
			for (const Indices& occupied : occupiedSets(count, count))
				stepLine(report, "U", routeSpec(uniform(level, count)), occupied);
		for (const Shape shape : {Shape::TwoOne, Shape::OneTwo})
			for (std::size_t h = 0; h < kLongRoute; ++h)
				stepLine(report, "U", routeSpec(uniform(level, kLongRoute), shape), {h});
	}
}

// The ordered pairs of different levels, and four more that include level 4.
std::vector<std::pair<int, int>> levelPairs() {
	const int levels[] = {kNoLevel, kAtb, kEtcsL1, kEtcsL2, kEtcsL3, kBacc};
	std::vector<std::pair<int, int>> pairs;
	for (const int west : levels)
		for (const int east : levels)
			if (west != east)
				pairs.emplace_back(west, east);
	pairs.insert(pairs.end(), {{kAtb, kVirtualCoupling}, {kVirtualCoupling, kAtb}, {kEtcsL3, kVirtualCoupling}, {kVirtualCoupling, kEtcsL3}});
	return pairs;
}

// Two levels with the border at section b and one occupied section h, which is the first section of the second
// level or lies behind it.
void groupBorder(Report& report) {
	report.comment("B: two levels, the first b sections of the first level, one occupied section h with b <= h, dirty start");
	for (const auto& [west, east] : levelPairs())
		for (std::size_t h = 1; h < kShortRoute; ++h)
			for (std::size_t b = 1; b <= h; ++b)
				stepLine(report, "B", routeSpec(twoLevels(west, east, b, kShortRoute)), {h}, " b=" + std::to_string(b));
}

// A train across the border and two trains on either side of it.
void groupTwoTrains(Report& report) {
	report.comment("T: two levels, border at section 3, two occupied sections, dirty start");
	for (const auto& [west, east] : levelPairs())
		for (const Indices& occupied : {Indices{2, 3}, Indices{1, 4}})
			stepLine(report, "T", routeSpec(twoLevels(west, east, 3, kShortRoute)), occupied, " b=3");
}

// Three or more levels on one route.
void groupMixed(Report& report) {
	report.comment("R: three or more levels on one route, dirty start");
	// Sections without a level in front of sections of levels 1 and 2.
	const std::vector<Levels> withoutLevel = {
		{kNoLevel, kNoLevel, kNoLevel, kEtcsL1, kEtcsL2, kEtcsL2},
		{kNoLevel, kNoLevel, kNoLevel, kEtcsL2, kEtcsL1, kEtcsL1},
	};
	for (const Levels& levels : withoutLevel)
		for (const Indices& occupied : {Indices{4}, Indices{3, 4}})
			stepLine(report, "R", routeSpec(levels), occupied);
	// Every level once, in both directions.
	const std::vector<Levels> allLevels = {
		{kAtb, kEtcsL1, kEtcsL2, kEtcsL3, kVirtualCoupling, kBacc},
		{kBacc, kVirtualCoupling, kEtcsL3, kEtcsL2, kEtcsL1, kAtb},
	};
	for (const Levels& levels : allLevels)
		for (std::size_t h = 0; h < kShortRoute; ++h)
			stepLine(report, "R", routeSpec(levels), {h});
	stepLine(report, "R", routeSpec({kBacc, kBacc, kAtb, kAtb, kEtcsL1, kEtcsL2}), {1, 3, 5});
}

// The ids in BlocksConnected for a train that covers the sections in covered. Train::Det_Section_Occupied_By_Train
// calls occupyBlockAndConnected for each covered section from the tail to the head, and that hands the section before
// it (section 0 for section 0) to releaseLastBlockAndConnected, which appends the id once. So the list holds the
// index max(h - 1, 0) for each covered h, each id once, in the order of first appearance.
Ids connectedBehind(const Indices& covered) {
	Ids connected;
	for (const std::size_t h : covered) {
		const std::string id = idOf(h == 0 ? 0 : h - 1);
		if (std::find(connected.begin(), connected.end(), id) == connected.end())
			connected.push_back(id);
	}
	return connected;
}

// A train that covers two sections moves through the route one section per step, leaves it, and one more step follows.
// The sections keep what the steps wrote.
void sweepLine(Report& report, const std::string& label, std::size_t routeCount, bool unchanged) {
	report.expect(unchanged, "the lists changed in the step of " + label);
	report.line(label + " | " + routesText(routeCount));
}

void sweep(Report& report, const Levels& levels) {
	const RouteSpec spec = routeSpec(levels, Shape::One, Start::Clean);
	const std::string head = labelOf("S", {spec});
	installRoutes({spec});
	const std::vector<Indices> trainPositions = {{0}, {0, 1}, {1, 2}, {2, 3}, {3, 4}, {4, 5}};
	std::size_t step = 0;
	for (const Indices& covered : trainPositions) {
		const bool unchanged = runStep(idsOf(covered), connectedBehind(covered));
		sweepLine(report, head + " step=" + std::to_string(step++) + " occ=" + indicesText(covered), 1, unchanged);
	}
	const std::string last = idOf(kShortRoute - 1);
	const Ids connected = runLastSection(last);
	const bool unchanged = runStep(Ids(), connected);
	sweepLine(report, head + " step=" + std::to_string(step++) + " occ=- left=" + last, 1, unchanged);
	sweepLine(report, head + " step=" + std::to_string(step) + " occ=-", 1, runStep(Ids(), Ids()));
}

void groupSweeps(Report& report) {
	report.comment("S: a train of two sections moves through a route of six sections, one step per line; the train has left in step 6 (left=)");
	report.comment("and nothing is occupied in step 7; clean start, nothing is reset between the steps");
	for (const int level : kAllLevels)
		sweep(report, uniform(level, kShortRoute));
	const std::pair<int, int> borders[] = {
		{kAtb, kEtcsL2},
		{kEtcsL2, kAtb},
		{kAtb, kBacc},
		{kBacc, kAtb},
		{kAtb, kEtcsL3},
		{kEtcsL3, kAtb},
		{kNoLevel, kEtcsL1},
		{kEtcsL1, kNoLevel},
	};
	for (const auto& [west, east] : borders)
		sweep(report, twoLevels(west, east, 3, kShortRoute));
}

// Only the release.
void groupRelease(Report& report) {
	report.comment("L: only releaseMixedSignallingSystem with the sections in connected= in BlocksConnected, nothing occupied, dirty start");
	const std::vector<RouteSpec> specs = {
		routeSpec(uniform(kNoLevel, kLongRoute)),
		routeSpec({kNoLevel, kAtb, kEtcsL1, kEtcsL2, kEtcsL3, kVirtualCoupling, kBacc}),
		routeSpec(uniform(kAtb, kLongRoute), Shape::TwoOne),
	};
	std::vector<Indices> connectedSets;
	for (std::size_t j = 0; j < kLongRoute; ++j)
		connectedSets.push_back({j});
	connectedSets.push_back({2, 5});
	connectedSets.push_back({0, 6});
	std::vector<Ids> idSets;
	for (const Indices& set : connectedSets)
		idSets.push_back(idsOf(set));
	idSets.push_back({"s9"}); // not on the route
	for (const RouteSpec& spec : specs) {
		for (const Ids& connected : idSets) {
			const std::string label = labelOf("L", {spec}) + " connected=" + idsText(connected);
			installRoutes({spec});
			report.expect(runRelease(connected), "the lists changed in the release of " + label);
			report.line(label + " | " + routesText(1));
		}
	}
}

// The last train leaves the routes at section id.
void lastSectionLine(Report& report, const std::vector<RouteSpec>& specs, const std::string& id) {
	const std::string label = labelOf("E", specs) + " left=" + id;
	installRoutes(specs);
	const Ids connected = runLastSection(id);
	report.line(label + " | " + routesText(specs.size()) + " => connected=" + idsText(connected));
}

void groupLastSection(Report& report) {
	report.comment("E: relLastSectionMixedSignalling(left=), dirty start; connected= is what the call leaves in BlocksConnected");
	for (const int level : kAllLevels)
		for (std::size_t b = 0; b < kShortRoute; ++b)
			lastSectionLine(report, {routeSpec(uniform(level, kShortRoute))}, idOf(b));
	for (std::size_t b = 0; b < kShortRoute; ++b)
		lastSectionLine(report, {routeSpec({kEtcsL1, kEtcsL1, kAtb, kAtb, kBacc, kBacc})}, idOf(b));
	lastSectionLine(report, {routeSpec(uniform(kAtb, kShortRoute))}, "s9"); // not on the route
	RouteSpec back = routeSpec(uniform(kBacc, kShortRoute));
	back.reversedIds = true;
	lastSectionLine(report, {routeSpec(uniform(kAtb, kShortRoute)), back}, idOf(kShortRoute - 1));
}

// Other signal speeds.
void groupSignalSpeeds(Report& report) {
	report.comment("V: signalCode1 = 14.5 and signalCode2 = 3.5, dirty start");
	const std::pair<double, double> saved = signalSpeeds();
	setSignalSpeeds({kOtherSignalCode1, kOtherSignalCode2});
	const std::string signals = " signals=" + numberText(kOtherSignalCode1) + "/" + numberText(kOtherSignalCode2);
	for (const int level : kAllLevels)
		for (const std::size_t h : {std::size_t{2}, std::size_t{5}})
			stepLine(report, "V", routeSpec(uniform(level, kShortRoute)), {h}, signals);
	setSignalSpeeds(saved);
}

// A single-track zone from s1 to s4 that a train holds. The routes that run against the holder see its sections as
// occupied during the activation.
void groupSingleTrack(Report& report) {
	report.comment("K: single-track zone s1 to s4 held in direction held=, nothing occupied, dirty start");
	report.comment("route 0 runs s0 to s5 and route 1 runs s5 to s0 against the holder at held=1; the routes against the holder see s1 to s4 occupied");
	for (const int level : kAllLevels) {
		std::vector<RouteSpec> specs = {routeSpec(uniform(level, kShortRoute)), routeSpec(uniform(level, kShortRoute))};
		specs[1].reversedIds = true;
		specs[1].reversedDirection = true;
		for (const int held : {1, -1, 0}) {
			const std::string label = labelOf("K", specs) + " held=" + std::to_string(held);
			installRoutes(specs);
			holdSingleTrack(held);
			report.expect(runStep(Ids(), Ids()), "the lists changed in the step of " + label);
			report.line(label + " | " + routesText(specs.size()));
		}
	}
	freeSingleTrack();
}

void writeHeader(Report& report) {
	const std::pair<double, double> speeds = signalSpeeds();
	report.comment("Aspects of the mixed signalling on routes built by hand, written by test_signalling_aspects.");
	report.comment("A line is: <group> <parameters> | <one token per section in route order>. A token is code:state:signal:braking:exit.");
	report.comment("  code     Section::code");
	report.comment("  state    G green, Y yellow, R red, RR red_red; any other state text as it is");
	report.comment("  signal   signalSpeedLimit of the arcs of the section in arc order, joined by '/'; one value when all arcs have it");
	report.comment("  braking  speedInBraking of the arcs of the section in arc order, joined by '/'");
	report.comment("  exit     exit_speed of the section");
	report.comment("Parameters: levels has one character per section (u = no level, 0 to 5 = the level) and a second route follows after '/';");
	report.comment("  shape is the number of arcs of the sections (1 = one each, 21 = 2 1 2 1 ..., 12 = 1 2 1 2 ...);");
	report.comment("  occ lists the occupied sections by index, connected lists the ids in BlocksConnected, left is the id given to");
	report.comment("  relLastSectionMixedSignalling, held is singleTrackHeld, signals are signalCode1/signalCode2.");
	report.comment("The ids of the sections are s0, s1 ... in route order. A second route lists the ids in reverse order; ' || ' separates the routes.");
	report.comment("Start states. Clean: code 270, state green, signal speed limit 999 on every arc, braking 0, exit 0.");
	report.comment("  Dirty: code " + numberText(kDirtyCode) + ", state " + kDirtyState + ", signal speed limit " + numberText(kDirtySignalSpeed)
		+ " on every arc, braking 0, exit " + numberText(kDirtyExitSpeed) + ".");
	report.comment("  No routine writes the dirty values, so a section that no routine wrote still shows them.");
	std::string limits;
	for (const double limit : kSectionSpeed)
		limits += (limits.empty() ? "" : ", ") + numberText(limit);
	report.comment("Speed limit of arc a of section i in m/s: {" + limits + "}[i] - " + numberText(kArcSpeedStep) + " * a.");
	report.comment("  They are numbers of the test, not railway data.");
	report.comment("signalCode1 = " + numberText(speeds.first) + " and signalCode2 = " + numberText(speeds.second) + " in every group except V.");
	report.comment("Groups:");
	report.comment("  U  one level on all sections, one step");
	report.comment("  B  two levels with the border at section b, one occupied section h with b <= h");
	report.comment("  T  two levels with the border at section 3, two occupied sections");
	report.comment("  R  three or more levels on one route");
	report.comment("  S  a train of two sections moves through a route, one step per line, clean start");
	report.comment("  L  only the release");
	report.comment("  E  the release of the last section of the route");
	report.comment("  V  other signal speeds");
	report.comment("  K  a held single-track zone seen from two routes");
}

Report buildReport() {
	Report report;
	const SavedGlobals saved;
	checkAnchors(report);
	writeHeader(report);
	groupUniform(report);
	groupBorder(report);
	groupTwoTrains(report);
	groupMixed(report);
	groupSweeps(report);
	groupRelease(report);
	groupLastSection(report);
	groupSignalSpeeds(report);
	groupSingleTrack(report);
	return report;
}

// ---------------------------------------------------------------------------
// The golden file
// ---------------------------------------------------------------------------

struct Row {
	std::size_t number; // line number in the file
	std::string text;
};

// The lines that count: not empty and not a comment.
std::vector<Row> caseRows(const std::vector<std::string>& lines) {
	std::vector<Row> rows;
	for (std::size_t i = 0; i < lines.size(); ++i)
		if (!lines[i].empty() && lines[i][0] != '#')
			rows.push_back({i + 1, lines[i]});
	return rows;
}

bool readLines(const std::string& path, std::vector<std::string>& lines) {
	std::ifstream in(path);
	if (!in)
		return false;
	std::string text;
	while (std::getline(in, text)) {
		if (!text.empty() && text.back() == '\r')
			text.pop_back();
		lines.push_back(text);
	}
	return true;
}

bool writeText(const std::string& path, const std::vector<std::string>& lines) {
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	for (const std::string& line : lines)
		out << line << '\n';
	return static_cast<bool>(out);
}

// Prints the first differences between the golden lines and the lines of this run.
bool sameRows(const std::vector<Row>& expected, const std::vector<Row>& actual) {
	const std::size_t common = std::min(expected.size(), actual.size());
	std::size_t differing = 0;
	for (std::size_t i = 0; i < common; ++i) {
		if (expected[i].text == actual[i].text)
			continue;
		if (++differing <= kMaxShown)
			std::cerr << "  line " << expected[i].number << "\n    expected: " << expected[i].text << "\n    actual:   " << actual[i].text << "\n";
	}
	if (differing > 0)
		std::cerr << "  " << differing << " differing lines\n";
	if (expected.size() != actual.size())
		std::cerr << "  expected " << expected.size() << " lines, actual " << actual.size() << "\n";
	return differing == 0 && expected.size() == actual.size();
}

} // namespace

int main(int argc, char** argv) {
	std::string expectPath;
	for (int i = 1; i < argc; ++i) {
		const std::string arg = argv[i];
		if (arg == "--expect" && i + 1 < argc) {
			expectPath = argv[++i];
		} else {
			std::cerr << "unknown argument '" << arg << "'\n";
			return 2;
		}
	}
	if (expectPath.empty()) {
		std::cerr << "usage: test_signalling_aspects --expect FILE\n";
		return 2;
	}
	const char* updateValue = std::getenv("EGTRAIN_UPDATE_EXPECTATIONS");
	const bool update = updateValue && std::string(updateValue) == "1";
	const char* ci = std::getenv("CI");
	if (update && ci && *ci) {
		std::cerr << "EGTRAIN_UPDATE_EXPECTATIONS=1 is refused when CI is set\n";
		return 1;
	}

	const Report report = buildReport();
	if (report.failed > kMaxShown)
		std::cerr << "failed: ... and " << report.failed - kMaxShown << " more\n";
	if (!report.ok)
		std::cerr << "FAIL signalling aspects: a check that does not read the golden file failed\n";

	const std::vector<Row> actual = caseRows(report.rows);
	if (update) {
		if (!report.ok)
			return 1;
		if (!writeText(expectPath, report.rows)) {
			std::cerr << "cannot write " << expectPath << "\n";
			return 1;
		}
		std::cout << "UPDATED " << expectPath << " (" << actual.size() << " cases)\n";
		return 0;
	}

	bool passed = report.ok;
	std::vector<std::string> goldenLines;
	if (!readLines(expectPath, goldenLines)) {
		std::cerr << "FAIL signalling aspects: missing or unreadable golden file " << expectPath
				  << "\n  run with EGTRAIN_UPDATE_EXPECTATIONS=1 to record it\n";
		passed = false;
	} else if (!sameRows(caseRows(goldenLines), actual)) {
		writeText(kActualFile, report.rows);
		std::cerr << "FAIL signalling aspects: output differs from " << expectPath << "\n  actual output written to " << kActualFile << "\n";
		passed = false;
	}
	if (!passed)
		return 1;
	std::cout << "PASS signalling aspects: " << actual.size() << " cases\n";
	return 0;
}
