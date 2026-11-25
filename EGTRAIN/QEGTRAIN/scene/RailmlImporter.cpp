#include "scene/RailmlImporter.h"

#include "io/third_party/pugixml.hpp"
#include "scene/RuntimeLimits.h"
#include "scene/SceneWriter.h"
#include "scene/SectionInventory.h"
#include "scene/Utf8Path.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <deque>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <functional>
#include <iterator>
#include <limits>
#include <locale>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// docs/architecture/railml-import.md describes the mapping. The fixed values below are listed there.

namespace {

using pugi::xml_node;

constexpr std::size_t kNone = std::numeric_limits<std::size_t>::max();

// Layout.
constexpr double kConnectionGapKm = 0.001; // length of a connection where the layout leaves no slack
constexpr double kSamePositionM = 0.5;	   // two railML positions closer than this are one point
constexpr double kMinimumBlockM = 1.0;	   // a signal closer than this to a track end does not start a block

// Values that the file may lack.
constexpr double kDefaultSpeedLimitKmh = 100.0; // stretch that no speed change covers
constexpr double kDefaultMaxSpeedKmh = 100.0;	// unit without a speed
constexpr double kDefaultLengthM = 70.0;		// unit without a length

// The reference unit of the defaults: SLT_Sprinter of the bundled cases (resources/rolling_stock_library.json).
constexpr double kReferenceMassKg = 151000.0;
constexpr double kReferenceDecelerationMs2 = 0.75;
constexpr double kReferenceFrontalAreaM2 = 1.45;
constexpr double kReferenceResistanceCoefficient = 0.004;
constexpr double kReferenceJerkMs3 = 0.75;
constexpr double kReferenceBreakMs = 8.611111111111;
constexpr double kReferenceTopMs = 36.111111111111;
constexpr double kReferenceStartForceN = 209000.0;
constexpr std::array<double, 3> kReferenceCurve = {324607.2915, -17671.4923, 292.9005};

// Capacities: a block at a track end shares a switch section with the block across the connection, so it keeps fewer arcs.
constexpr std::size_t kInteriorBlockArcs = RuntimeLimits::kNativeMaxSectionArcs;
constexpr std::size_t kEndBlockArcs = (RuntimeLimits::kNativeMaxSectionArcs - 1) / 2;

constexpr double kDurationMarginSeconds = 600.0;
constexpr std::size_t kListed = 8; // ids named in one diagnostic

constexpr const char* kDefaultUnitId = "railml_default";

// --- Text ---

std::string trimmed(const std::string& text) {
	const std::size_t begin = text.find_first_not_of(" \t\r\n");
	if (begin == std::string::npos)
		return "";
	const std::size_t end = text.find_last_not_of(" \t\r\n");
	return text.substr(begin, end - begin + 1);
}

// A finite number in the C locale, the whole text; empty when the text is not one.
std::optional<double> parseNumber(const std::string& raw) {
	const std::string text = trimmed(raw);
	if (text.empty())
		return std::nullopt;
	std::istringstream input(text);
	input.imbue(std::locale::classic());
	double value = 0.0;
	input >> value;
	if (input.fail())
		return std::nullopt;
	input >> std::ws;
	if (!input.eof() || !std::isfinite(value))
		return std::nullopt;
	return value;
}

// Seconds of an xs:time "HH:MM:SS" with optional fraction and a trailing Z.
std::optional<double> parseClock(const std::string& raw) {
	std::string text = trimmed(raw);
	if (!text.empty() && (text.back() == 'Z' || text.back() == 'z'))
		text.pop_back();
	const std::size_t first = text.find(':');
	const std::size_t second = first == std::string::npos ? std::string::npos : text.find(':', first + 1);
	if (second == std::string::npos || text.find(':', second + 1) != std::string::npos)
		return std::nullopt;
	const auto hours = parseNumber(text.substr(0, first));
	const auto minutes = parseNumber(text.substr(first + 1, second - first - 1));
	const auto seconds = parseNumber(text.substr(second + 1));
	if (!hours || !minutes || !seconds || *hours < 0.0 || std::floor(*hours) != *hours || *minutes < 0.0
		|| *minutes >= 60.0 || std::floor(*minutes) != *minutes || *seconds < 0.0 || *seconds >= 61.0)
		return std::nullopt;
	return *hours * 3600.0 + *minutes * 60.0 + *seconds;
}

// Seconds of an xs:duration such as "PT1M12.5S" or "P0DT0H0M30S". Years and months have no fixed length and are refused.
std::optional<double> parseDuration(const std::string& raw) {
	std::string text = trimmed(raw);
	if (text.empty() || text[0] != 'P')
		return std::nullopt;
	double total = 0.0;
	bool inTime = false;
	bool any = false;
	std::string digits;
	for (std::size_t index = 1; index < text.size(); ++index) {
		const char c = text[index];
		if (c == 'T') {
			if (inTime || !digits.empty())
				return std::nullopt;
			inTime = true;
			continue;
		}
		if ((c >= '0' && c <= '9') || c == '.') {
			digits += c;
			continue;
		}
		const auto value = parseNumber(digits);
		digits.clear();
		if (!value)
			return std::nullopt;
		any = true;
		if (!inTime && c == 'D')
			total += *value * 86400.0;
		else if (inTime && c == 'H')
			total += *value * 3600.0;
		else if (inTime && c == 'M')
			total += *value * 60.0;
		else if (inTime && c == 'S')
			total += *value;
		else
			return std::nullopt;
	}
	if (!digits.empty() || !any)
		return std::nullopt;
	return total;
}

std::string clockText(long long seconds) {
	seconds %= 86400;
	if (seconds < 0)
		seconds += 86400;
	char text[16];
	std::snprintf(text, sizeof text, "%02lld:%02lld:%02lld", seconds / 3600, (seconds / 60) % 60, seconds % 60);
	return text;
}

std::string numberText(double value) {
	std::ostringstream text;
	text.imbue(std::locale::classic());
	text << std::setprecision(10) << value;
	return text.str();
}

std::string namedList(const std::vector<std::string>& ids) {
	std::string text;
	for (std::size_t index = 0; index < ids.size() && index < kListed; ++index)
		text += (index == 0 ? "" : ", ") + ids[index];
	if (ids.size() > kListed)
		text += " and " + std::to_string(ids.size() - kListed) + " more";
	return text;
}

double round6(double value) {
	return std::round(value * 1e6) / 1e6;
}

// --- XML ---

const char* localName(const char* name) {
	const char* colon = std::strrchr(name, ':');
	return colon == nullptr ? name : colon + 1;
}

bool named(const xml_node& node, const char* name) {
	return node.type() == pugi::node_element && std::strcmp(localName(node.name()), name) == 0;
}

xml_node firstChild(const xml_node& node, const char* name) {
	for (const xml_node& child : node.children())
		if (named(child, name))
			return child;
	return {};
}

std::vector<xml_node> childrenNamed(const xml_node& node, const char* name) {
	std::vector<xml_node> result;
	for (const xml_node& child : node.children())
		if (named(child, name))
			result.push_back(child);
	return result;
}

std::vector<xml_node> elementChildren(const xml_node& node) {
	std::vector<xml_node> result;
	for (const xml_node& child : node.children())
		if (child.type() == pugi::node_element)
			result.push_back(child);
	return result;
}

xml_node firstDescendant(const xml_node& node, const char* name) {
	for (const xml_node& child : node.children()) {
		if (named(child, name))
			return child;
		if (const xml_node found = firstDescendant(child, name))
			return found;
	}
	return {};
}

void collectDescendants(const xml_node& node, const char* name, std::vector<xml_node>& result) {
	for (const xml_node& child : node.children()) {
		if (named(child, name))
			result.push_back(child);
		else
			collectDescendants(child, name, result);
	}
}

std::string attr(const xml_node& node, const char* name) {
	return trimmed(node.attribute(name).value());
}

std::optional<double> numberAttr(const xml_node& node, const char* name) {
	return parseNumber(node.attribute(name).value());
}

int directionAttr(const xml_node& node) {
	const std::string dir = attr(node, "dir");
	if (dir == "up")
		return 1;
	if (dir == "down")
		return -1;
	return 0;
}

// --- railML records ---

struct Change {
	std::string id;
	double pos = 0.0;
	int dir = 0; // 1 up, -1 down, 0 both or unknown
	double value = 0.0;
	bool end = false; // vMax="end": the limit before the last change applies again
	std::string profile;
};

struct PlatformEdge {
	std::string id;
	std::string ocp;
	double pos = 0.0;
	double length = 0.0;
	bool hasLength = false;
	std::string stationOcp; // the station it is assigned to; empty when none
	double stopPos = 0.0;
};

struct Signal {
	std::string id;
	std::string type;
	double pos = 0.0;
	int dir = 0;
};

// A connection element at a track end or in a switch or crossing.
struct ConnectionElement {
	std::string id;
	std::string ref;
	std::string orientation;
	std::string owner; // id of the switch or crossing; empty at a track end
	bool inCrossing = false;
	std::size_t track = 0;
	double pos = 0.0;
	int place = 0;			 // -1 track begin, 1 track end, 0 inside
	std::size_t end = kNone; // piece end it joins
};

struct Track {
	std::string id;
	double beginPos = 0.0;
	double endPos = 0.0;
	std::string beginOcp; // ocpRef of a macroscopicNode at the begin
	std::string endOcp;
	bool beginConnected = false;
	bool endConnected = false;
	std::vector<std::pair<double, std::string>> crossSections; // position, OCP
	std::vector<Change> speed;
	std::vector<Change> gradient;
	std::vector<Change> radius;
	std::vector<PlatformEdge> platforms;
	std::vector<Signal> signals;
	std::vector<std::size_t> pieces; // in position order
	bool hasConnection = false;		 // a connection element at an end or in a switch or crossing
};

struct Ocp {
	std::string id;
	std::string name;
	bool hasGeo = false;
	double latitude = 0.0;
	double longitude = 0.0;
};

struct Vehicle {
	std::string id;
	std::optional<double> lengthM;
	std::optional<double> speedKmh;
	std::optional<double> massT;
	bool massFromBrutto = false;
	bool powered = false;
	int places = 0;
};

struct Formation {
	std::string id;
	std::optional<double> lengthM;
	std::optional<double> speedKmh;
	std::optional<double> weightT;
	std::vector<std::pair<std::string, int>> vehicles; // id and count, in train order
};

struct TrackRef {
	std::string track;
	int dir = 0;
};

struct OcpVisit {
	std::string ocp;
	std::string type;
	std::optional<double> arrival;
	std::optional<double> departure;
	std::optional<double> minimalStop;
	std::vector<TrackRef> section;
};

struct TrainPart {
	std::string id;
	std::string number;
	std::string category;
	std::string formation;
	std::string period;
	std::optional<double> speedKmh;
	std::vector<OcpVisit> visits;
};

struct TrainSequence {
	int sequence = 0;
	std::vector<std::pair<int, std::string>> parts; // position and train part id
	std::string category;
	std::string speedProfile;
};

struct Train {
	std::string id;
	std::string number;
	std::string type;
	std::vector<TrainSequence> sequences;
};

// --- Conversion state ---

struct Piece {
	std::string id;
	std::size_t track = 0;
	double beginPos = 0.0;
	double endPos = 0.0;
	int sign = 1; // 1: chainage grows with the railML position
	double xLow = 0.0;
	std::vector<double> boundaries;	   // block boundaries, ends included, ascending position
	std::vector<std::string> blockIds; // by ascending position
	std::vector<double> nodePos;	   // ascending position
	std::vector<std::string> nodeIds;  // matching nodePos
	std::string beginOcp;			   // OCP of a macroscopic node or a junction at the begin; empty when none
	std::string endOcp;
	int region = 0;
	int level = 0;

	double lengthKm() const { return (endPos - beginPos) / 1000.0; }
	double x(double pos) const {
		return round6(sign > 0 ? xLow + (pos - beginPos) / 1000.0 : xLow + (endPos - pos) / 1000.0);
	}
	double xHigh() const { return round6(xLow + lengthKm()); }
	double sidePos(int side) const { return side == 0 ? beginPos : endPos; }
};

std::size_t pieceOf(std::size_t end) { return end / 2; }
int sideOf(std::size_t end) { return static_cast<int>(end % 2); }

struct Link {
	std::size_t endA = 0;
	std::size_t endB = 0;
	int priority = 0; // 0 within a track, 1 connection or train movement, 2 node of a macroscopic network
	int weight = 0;	  // train movements over it
	std::string id;
	std::string place;				   // what a person can find in the file: connection ids or the OCP
	std::vector<std::size_t> elements; // connection elements it comes from
	bool accepted = false;
	std::size_t from = kNone; // piece whose high chainage end it joins
	std::size_t to = kNone;	  // piece whose low chainage end it joins
	std::string connectionId;
};

struct Leg {
	std::size_t piece = 0;
	int dir = 1; // 1 up, -1 down, in railML positions
	double fromPos = 0.0;
	double toPos = 0.0;
};

struct PiecePath {
	std::vector<Leg> legs;
	std::vector<std::size_t> joins; // link between legs[i] and legs[i + 1]
};

struct ServiceDraft {
	SceneService service;
	std::vector<std::string> partIds;
	std::vector<double> stopArrival;   // absolute seconds, NaN when none
	std::vector<double> stopDeparture; // absolute seconds, NaN when none
	std::vector<std::string> stopStations;
	std::vector<double> stopDwell;
	bool hasEntry = false;
	double entry = 0.0;
	std::vector<std::string> routeBlocks;
	bool reversed = false;
};

struct Context {
	std::string sourceFile;
	RailmlImportOptions options;
	std::string version;
	SceneImportResult result;
	std::deque<SceneImportReportRow> report; // a deque, so that a row reference stays valid while rows are added
	SceneModel scene;

	std::vector<Track> tracks;
	std::unordered_map<std::string, std::size_t> trackIndex;
	std::vector<ConnectionElement> connections;
	std::vector<Ocp> ocps;
	std::unordered_map<std::string, std::size_t> ocpIndex;
	std::vector<Vehicle> vehicles;
	std::unordered_map<std::string, std::size_t> vehicleIndex;
	std::vector<Formation> formations;
	std::unordered_map<std::string, std::size_t> formationIndex;
	std::map<std::string, std::string> periods;	   // id to name
	std::map<std::string, std::string> categories; // id to code
	std::vector<TrainPart> parts;
	std::unordered_map<std::string, std::size_t> partIndex;
	std::vector<Train> trains;
	std::map<std::string, int> unsupported; // element kind to count
	std::string preferredSpeedProfile;
	std::string period; // the operating period imported; empty when the file has none

	std::vector<Piece> pieces;
	std::vector<Link> links;
	std::vector<std::vector<std::size_t>> linksAtEnd;								 // accepted links per piece end
	bool signalBlocks = false;														 // blocks from main signals; otherwise from OCPs, with generated signals
	std::map<std::pair<std::size_t, std::string>, std::vector<double>> ocpPositions; // track and OCP to positions
	std::unordered_map<std::string, std::string> nodeOwner;
};

// The report row of an element kind. The kind is a C string, so that no temporary string outlives the call.
SceneImportReportRow& reportRow(Context& context, const char* kind) {
	const std::string category = std::string("railml:") + kind;
	for (auto& row : context.report)
		if (row.category == category)
			return row;
	SceneImportReportRow row;
	row.category = category;
	row.sourceFile = context.sourceFile;
	context.report.push_back(row);
	return context.report.back();
}

void addDiagnostic(Context& context, SceneSeverity severity, const std::string& code, const std::string& message,
	const std::string& itemType = "", const std::string& itemId = "", const std::string& suggestedFix = "") {
	SceneDiagnostic diagnostic;
	diagnostic.severity = severity;
	diagnostic.code = code;
	diagnostic.message = message;
	diagnostic.file = context.sourceFile;
	diagnostic.itemType = itemType;
	diagnostic.itemId = itemId;
	diagnostic.suggestedFix = suggestedFix;
	context.result.diagnostics.push_back(std::move(diagnostic));
}

// True when the train part runs in the imported operating period.
bool inPeriod(const Context& context, const TrainPart& part) {
	return part.period.empty() || context.period.empty() || part.period == context.period;
}

void reportDefault(Context& context, const std::string& kind, const std::string& message) {
	addDiagnostic(context, SceneSeverity::Info, "scene.import.railml.default", message, kind);
}

void countUnsupported(Context& context, const xml_node& element) {
	++context.unsupported[localName(element.name())];
}

// Counts every element below a group that the import does not map.
void countUnsupportedChildren(Context& context, const xml_node& group) {
	for (const xml_node& child : elementChildren(group))
		countUnsupported(context, child);
}

// --- Reading ---

bool checkVersion(Context& context, const xml_node& root) {
	std::string rootName = localName(root.name());
	std::transform(rootName.begin(), rootName.end(), rootName.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	if (!root || rootName != "railml") { // railML 3 names its root railML
		addDiagnostic(context, SceneSeverity::Error, "scene.import.railml.format",
			"The file is not railML: its root element is not railml", "file", context.sourceFile,
			"Choose a railML 2 file");
		return false;
	}
	const std::string name = root.name();
	const std::size_t colon = name.find(':');
	const std::string xmlns = colon == std::string::npos ? "xmlns" : "xmlns:" + name.substr(0, colon);
	const std::string ns = attr(root, xmlns.c_str());
	const std::string version = attr(root, "version");
	const bool railml3 = (!version.empty() && version[0] == '3') || ns.find("railml.org/schemas/3") != std::string::npos;
	bool namespace2 = false;
	for (const char* prefix : {"http://www.railml.org/schemas/20", "https://www.railml.org/schemas/20"}) {
		const std::size_t length = std::strlen(prefix);
		if (ns.size() == length + 2 && ns.compare(0, length, prefix) == 0 && std::isdigit(static_cast<unsigned char>(ns[length]))
			&& std::isdigit(static_cast<unsigned char>(ns[length + 1])))
			namespace2 = true;
	}
	const bool version2 = version.rfind("2.", 0) == 0;
	if (railml3 || !(version2 || (version.empty() && namespace2))) {
		addDiagnostic(context, SceneSeverity::Error, "scene.import.railml.version",
			railml3 ? "The file is railML 3, which the import does not read; only railML 2.x is supported"
					: "The file is not railML 2.x (version \"" + version + "\", namespace \"" + ns + "\")",
			"file", context.sourceFile, "Export the data as railML 2.x");
		return false;
	}
	context.version = version.empty() ? "2.x" : version;
	return true;
}

void readChange(Context& context, const xml_node& element, const char* valueName, std::vector<Change>& changes,
	const char* kind) {
	SceneImportReportRow& row = reportRow(context, kind);
	++row.sourceCount;
	Change change;
	change.id = attr(element, "id");
	change.dir = directionAttr(element);
	change.profile = attr(element, "profileRef");
	const auto pos = numberAttr(element, "pos");
	const std::string value = attr(element, valueName);
	const auto number = parseNumber(value);
	if (!pos || (!number && value != "end")) {
		++row.skippedCount;
		return;
	}
	change.pos = *pos;
	change.end = value == "end";
	change.value = number.value_or(0.0);
	changes.push_back(change);
}

void readTrackElements(Context& context, Track& track, const xml_node& trackElements) {
	for (const xml_node& group : elementChildren(trackElements)) {
		for (const xml_node& element : elementChildren(group)) {
			if (named(element, "speedChange"))
				readChange(context, element, "vMax", track.speed, "speedChange");
			else if (named(element, "gradientChange"))
				readChange(context, element, "slope", track.gradient, "gradientChange");
			else if (named(element, "radiusChange"))
				readChange(context, element, "radius", track.radius, "radiusChange");
			else if (named(element, "platformEdge")) {
				SceneImportReportRow& row = reportRow(context, "platformEdge");
				++row.sourceCount;
				PlatformEdge edge;
				edge.id = attr(element, "id");
				edge.ocp = attr(element, "ocpRef");
				const auto pos = numberAttr(element, "pos");
				const auto length = numberAttr(element, "length");
				if (edge.id.empty() || !pos) {
					++row.skippedCount;
					continue;
				}
				edge.pos = *pos;
				edge.hasLength = length && *length > 0.0;
				edge.length = edge.hasLength ? *length : 0.0;
				track.platforms.push_back(edge);
			} else
				countUnsupported(context, element);
		}
	}
}

void readOcsElements(Context& context, Track& track, const xml_node& ocsElements) {
	for (const xml_node& group : elementChildren(ocsElements)) {
		for (const xml_node& element : elementChildren(group)) {
			if (!named(element, "signal")) {
				countUnsupported(context, element);
				continue;
			}
			SceneImportReportRow& row = reportRow(context, "signal");
			++row.sourceCount;
			Signal signal;
			signal.id = attr(element, "id");
			signal.type = attr(element, "type");
			signal.dir = directionAttr(element);
			const auto pos = numberAttr(element, "pos");
			if (signal.id.empty() || !pos) {
				++row.skippedCount;
				continue;
			}
			signal.pos = *pos;
			track.signals.push_back(signal);
		}
	}
}

void readTrackEnd(Context& context, Track& track, const xml_node& element, int place) {
	for (const xml_node& child : elementChildren(element)) {
		if (named(child, "connection")) {
			ConnectionElement connection;
			connection.id = attr(child, "id");
			connection.ref = attr(child, "ref");
			connection.track = context.tracks.size();
			connection.place = place;
			context.connections.push_back(connection);
			(place < 0 ? track.beginConnected : track.endConnected) = true;
			track.hasConnection = true;
		} else if (named(child, "macroscopicNode")) {
			++reportRow(context, "macroscopicNode").sourceCount;
			(place < 0 ? track.beginOcp : track.endOcp) = attr(child, "ocpRef");
		} else if (named(child, "bufferStop") || named(child, "openEnd")) {
			SceneImportReportRow& row = reportRow(context, localName(child.name()));
			++row.sourceCount;
			++row.convertedCount; // a track end without a connection
		} else
			countUnsupported(context, child);
	}
}

void readTrack(Context& context, const xml_node& element) {
	SceneImportReportRow& row = reportRow(context, "track");
	++row.sourceCount;
	Track track;
	track.id = attr(element, "id");
	const xml_node topology = firstChild(element, "trackTopology");
	const xml_node begin = firstChild(topology, "trackBegin");
	const xml_node end = firstChild(topology, "trackEnd");
	const auto beginPos = numberAttr(begin, "pos");
	const auto endPos = numberAttr(end, "pos");
	if (track.id.empty() || !begin || !end || !endPos || context.trackIndex.count(track.id) > 0
		|| *endPos - beginPos.value_or(0.0) < kMinimumBlockM) {
		++row.skippedCount;
		addDiagnostic(context, SceneSeverity::Warning, "scene.import.railml.skipped",
			"Track " + (track.id.empty() ? std::string("without id") : track.id)
				+ " is skipped: it needs a unique id, a trackBegin and a trackEnd with positions, and a length of at least 1 m",
			"track", track.id);
		return;
	}
	track.beginPos = beginPos.value_or(0.0);
	track.endPos = *endPos;
	readTrackEnd(context, track, begin, -1);
	readTrackEnd(context, track, end, 1);
	for (const xml_node& child : elementChildren(topology)) {
		if (named(child, "trackBegin") || named(child, "trackEnd"))
			continue;
		if (named(child, "connections")) {
			for (const xml_node& owner : elementChildren(child)) {
				const bool isSwitch = named(owner, "switch");
				if (!isSwitch && !named(owner, "crossing")) {
					countUnsupported(context, owner);
					continue;
				}
				SceneImportReportRow& ownerRow = reportRow(context, isSwitch ? "switch" : "crossing");
				++ownerRow.sourceCount;
				const auto pos = numberAttr(owner, "pos");
				if (!pos) {
					++ownerRow.skippedCount;
					continue;
				}
				for (const xml_node& connectionElement : childrenNamed(owner, "connection")) {
					ConnectionElement connection;
					connection.id = attr(connectionElement, "id");
					connection.ref = attr(connectionElement, "ref");
					connection.orientation = attr(connectionElement, "orientation");
					connection.owner = attr(owner, "id");
					connection.inCrossing = !isSwitch;
					connection.track = context.tracks.size();
					connection.pos = *pos;
					connection.place = std::fabs(*pos - track.beginPos) <= kSamePositionM ? -1
																						  : (std::fabs(*pos - track.endPos) <= kSamePositionM ? 1 : 0);
					context.connections.push_back(connection);
					track.hasConnection = true;
				}
			}
		} else if (named(child, "crossSections")) {
			for (const xml_node& section : childrenNamed(child, "crossSection")) {
				SceneImportReportRow& sectionRow = reportRow(context, "crossSection");
				++sectionRow.sourceCount;
				const auto pos = numberAttr(section, "pos");
				const std::string ocp = attr(section, "ocpRef");
				if (!pos || ocp.empty()) {
					++sectionRow.skippedCount;
					continue;
				}
				track.crossSections.emplace_back(*pos, ocp);
			}
		} else
			countUnsupportedChildren(context, child);
	}
	for (const xml_node& child : elementChildren(element)) {
		if (named(child, "trackTopology"))
			continue;
		if (named(child, "trackElements"))
			readTrackElements(context, track, child);
		else if (named(child, "ocsElements"))
			readOcsElements(context, track, child);
		else if (!named(child, "trackDescr") && !named(child, "additionalName"))
			countUnsupported(context, child);
	}
	++row.convertedCount;
	context.trackIndex[track.id] = context.tracks.size();
	context.tracks.push_back(std::move(track));
}

void readOcps(Context& context, const xml_node& group) {
	for (const xml_node& element : childrenNamed(group, "ocp")) {
		Ocp ocp;
		ocp.id = attr(element, "id");
		ocp.name = attr(element, "name");
		++reportRow(context, "ocp").sourceCount;
		if (ocp.id.empty() || context.ocpIndex.count(ocp.id) > 0) {
			++reportRow(context, "ocp").skippedCount;
			continue;
		}
		if (const xml_node geo = firstDescendant(element, "geoCoord")) {
			SceneImportReportRow& geoRow = reportRow(context, "geoCoord");
			++geoRow.sourceCount;
			std::istringstream coord(attr(geo, "coord"));
			coord.imbue(std::locale::classic());
			double latitude = 0.0;
			double longitude = 0.0;
			const std::string epsg = attr(geo, "epsgCode");
			const bool wgs84 = epsg.empty() || (epsg.size() >= 4 && epsg.compare(epsg.size() - 4, 4, "4326") == 0);
			if (coord >> latitude >> longitude && wgs84 && !(latitude == 0.0 && longitude == 0.0)
				&& std::fabs(latitude) < 90.0 && std::fabs(longitude) <= 180.0) {
				ocp.hasGeo = true;
				ocp.latitude = latitude;
				ocp.longitude = longitude;
			} else {
				++geoRow.skippedCount;
			}
		}
		context.ocpIndex[ocp.id] = context.ocps.size();
		context.ocps.push_back(ocp);
	}
}

void readInfrastructure(Context& context, const xml_node& infrastructure) {
	for (const xml_node& child : elementChildren(infrastructure)) {
		if (named(child, "tracks")) {
			for (const xml_node& track : elementChildren(child)) {
				if (named(track, "track"))
					readTrack(context, track);
				else
					countUnsupported(context, track);
			}
		} else if (named(child, "operationControlPoints")) {
			readOcps(context, child);
		} else if (named(child, "speedProfiles")) {
			continue; // read through the speed changes that refer to them
		} else {
			countUnsupportedChildren(context, child);
		}
	}
}

void readRollingStock(Context& context, const xml_node& rollingStock) {
	for (const xml_node& child : elementChildren(rollingStock)) {
		if (named(child, "vehicles")) {
			for (const xml_node& element : childrenNamed(child, "vehicle")) {
				SceneImportReportRow& row = reportRow(context, "vehicle");
				++row.sourceCount;
				Vehicle vehicle;
				vehicle.id = attr(element, "id");
				if (vehicle.id.empty() || context.vehicleIndex.count(vehicle.id) > 0) {
					++row.skippedCount;
					continue;
				}
				vehicle.lengthM = numberAttr(element, "length");
				vehicle.speedKmh = numberAttr(element, "speed");
				vehicle.massT = numberAttr(element, "bruttoWeight");
				vehicle.massFromBrutto = vehicle.massT.has_value();
				if (!vehicle.massT)
					vehicle.massT = numberAttr(element, "tareWeight");
				if (!vehicle.massT)
					vehicle.massT = numberAttr(element, "nettoWeight");
				vehicle.powered = numberAttr(element, "numberDrivenAxles").value_or(0.0) > 0.0
					|| firstDescendant(element, "engine");
				std::vector<xml_node> places;
				collectDescendants(element, "places", places);
				for (const xml_node& place : places) {
					const std::string category = attr(place, "category");
					if (category == "bicycle" || category.rfind("other:", 0) == 0)
						continue;
					vehicle.places += static_cast<int>(std::max(0.0, numberAttr(place, "count").value_or(0.0)));
				}
				context.vehicleIndex[vehicle.id] = context.vehicles.size();
				context.vehicles.push_back(vehicle);
			}
		} else if (named(child, "formations")) {
			for (const xml_node& element : childrenNamed(child, "formation")) {
				SceneImportReportRow& row = reportRow(context, "formation");
				++row.sourceCount;
				Formation formation;
				formation.id = attr(element, "id");
				if (formation.id.empty() || context.formationIndex.count(formation.id) > 0) {
					++row.skippedCount;
					continue;
				}
				formation.lengthM = numberAttr(element, "length");
				formation.speedKmh = numberAttr(element, "speed");
				formation.weightT = numberAttr(element, "weight");
				std::vector<std::pair<double, std::pair<std::string, int>>> ordered;
				for (const xml_node& reference : childrenNamed(firstChild(element, "trainOrder"), "vehicleRef")) {
					const double order = numberAttr(reference, "orderNumber").value_or(static_cast<double>(ordered.size()));
					const int count = static_cast<int>(std::max(1.0, numberAttr(reference, "vehicleCount").value_or(1.0)));
					ordered.push_back({order, {attr(reference, "vehicleRef"), count}});
				}
				std::stable_sort(ordered.begin(), ordered.end(),
					[](const auto& left, const auto& right) { return left.first < right.first; });
				for (const auto& entry : ordered)
					formation.vehicles.push_back(entry.second);
				context.formationIndex[formation.id] = context.formations.size();
				context.formations.push_back(formation);
			}
		} else {
			countUnsupportedChildren(context, child);
		}
	}
}

// Reads the arrival and departure of a times element with their day offsets.
void readTimes(const xml_node& times, OcpVisit& visit) {
	const auto arrival = parseClock(attr(times, "arrival"));
	const auto departure = parseClock(attr(times, "departure"));
	if (arrival)
		visit.arrival = *arrival + 86400.0 * numberAttr(times, "arrivalDay").value_or(0.0);
	if (departure)
		visit.departure = *departure + 86400.0 * numberAttr(times, "departureDay").value_or(0.0);
}

void readTrainPart(Context& context, const xml_node& element) {
	SceneImportReportRow& row = reportRow(context, "trainPart");
	++row.sourceCount;
	TrainPart part;
	part.id = attr(element, "id");
	if (part.id.empty() || context.partIndex.count(part.id) > 0) {
		++row.skippedCount;
		return;
	}
	part.number = attr(element, "trainNumber");
	part.category = attr(element, "categoryRef");
	reportRow(context, "ocpTT").sourceCount += static_cast<int>(childrenNamed(firstChild(element, "ocpsTT"), "ocpTT").size());
	const xml_node formation = firstChild(element, "formationTT");
	part.formation = attr(formation, "formationRef");
	part.speedKmh = numberAttr(formation, "speed");
	part.period = attr(firstChild(element, "operatingPeriodRef"), "ref");
	std::vector<std::pair<double, OcpVisit>> ordered;
	for (const xml_node& ocpTT : childrenNamed(firstChild(element, "ocpsTT"), "ocpTT")) {
		OcpVisit visit;
		visit.ocp = attr(ocpTT, "ocpRef");
		visit.type = attr(ocpTT, "ocpType");
		xml_node chosen;
		for (const xml_node& times : childrenNamed(ocpTT, "times")) {
			if (!chosen || attr(times, "scope") == "scheduled")
				chosen = times;
			if (attr(times, "scope") == "scheduled")
				break;
		}
		if (chosen)
			readTimes(chosen, visit);
		visit.minimalStop = parseDuration(attr(firstChild(firstChild(ocpTT, "stopDescription"), "stopTimes"), "minimalTime"));
		for (const xml_node& section : childrenNamed(ocpTT, "sectionTT")) {
			std::vector<xml_node> references;
			collectDescendants(section, "trackRef", references);
			for (const xml_node& reference : references)
				visit.section.push_back({attr(reference, "ref"), directionAttr(reference)});
		}
		const double sequence = numberAttr(ocpTT, "sequence").value_or(static_cast<double>(ordered.size()));
		ordered.emplace_back(sequence, std::move(visit));
	}
	std::stable_sort(ordered.begin(), ordered.end(), [](const auto& left, const auto& right) { return left.first < right.first; });
	for (auto& entry : ordered)
		part.visits.push_back(std::move(entry.second));
	context.partIndex[part.id] = context.parts.size();
	context.parts.push_back(std::move(part));
}

void readTimetable(Context& context, const xml_node& timetable) {
	for (const xml_node& child : elementChildren(timetable)) {
		if (named(child, "operatingPeriods")) {
			for (const xml_node& element : childrenNamed(child, "operatingPeriod")) {
				++reportRow(context, "operatingPeriod").sourceCount;
				const std::string id = attr(element, "id");
				if (!id.empty())
					context.periods[id] = attr(element, "name");
			}
		} else if (named(child, "categories")) {
			for (const xml_node& element : childrenNamed(child, "category")) {
				++reportRow(context, "category").sourceCount;
				const std::string code = attr(element, "code");
				context.categories[attr(element, "id")] = code.empty() ? attr(element, "name") : code;
			}
		} else if (named(child, "trainParts")) {
			for (const xml_node& element : childrenNamed(child, "trainPart"))
				readTrainPart(context, element);
		} else if (named(child, "trains")) {
			for (const xml_node& element : childrenNamed(child, "train")) {
				++reportRow(context, "train").sourceCount;
				Train train;
				train.id = attr(element, "id");
				train.number = attr(element, "trainNumber");
				train.type = attr(element, "type");
				for (const xml_node& sequenceElement : childrenNamed(element, "trainPartSequence")) {
					TrainSequence sequence;
					sequence.sequence = static_cast<int>(numberAttr(sequenceElement, "sequence").value_or(0.0));
					sequence.category = attr(sequenceElement, "categoryRef");
					sequence.speedProfile = attr(firstChild(sequenceElement, "speedProfileRef"), "ref");
					for (const xml_node& reference : childrenNamed(sequenceElement, "trainPartRef"))
						sequence.parts.emplace_back(static_cast<int>(numberAttr(reference, "position").value_or(0.0)),
							attr(reference, "ref"));
					std::stable_sort(sequence.parts.begin(), sequence.parts.end(),
						[](const auto& left, const auto& right) { return left.first < right.first; });
					train.sequences.push_back(std::move(sequence));
				}
				std::stable_sort(train.sequences.begin(), train.sequences.end(),
					[](const TrainSequence& left, const TrainSequence& right) { return left.sequence < right.sequence; });
				context.trains.push_back(std::move(train));
			}
		} else if (!named(child, "timetablePeriods")) {
			countUnsupportedChildren(context, child);
		}
	}
}

// --- Pieces and links ---

// A stretch of a train part over one track, and the OCP where it joins the stretch before it.
struct TrackRun {
	std::size_t track = 0;
	int dir = 0;		  // 1 up, -1 down
	std::string junction; // OCP of the visit whose section starts this run; empty for the first run or inside a section
};

// The runs of a train part over its tracks, from trackRef dir, or from the positions of the OCPs at both ends of the
// section when dir is missing. Consecutive references of one track in one direction are one run.
std::vector<TrackRun> partTracks(const Context& context, const TrainPart& part, bool& unresolved) {
	std::vector<TrackRun> result;
	unresolved = false;
	for (std::size_t index = 0; index + 1 < part.visits.size(); ++index) {
		const OcpVisit& visit = part.visits[index];
		if (visit.section.empty())
			unresolved = true;
		for (std::size_t referenceIndex = 0; referenceIndex < visit.section.size(); ++referenceIndex) {
			const TrackRef& reference = visit.section[referenceIndex];
			const auto track = context.trackIndex.find(reference.track);
			if (track == context.trackIndex.end()) {
				unresolved = true;
				continue;
			}
			int dir = reference.dir;
			if (dir == 0) {
				const auto from = context.ocpPositions.find({track->second, visit.ocp});
				const auto to = context.ocpPositions.find({track->second, part.visits[index + 1].ocp});
				if (from != context.ocpPositions.end() && to != context.ocpPositions.end())
					dir = to->second.front() > from->second.front() ? 1 : -1;
			}
			if (dir == 0) {
				unresolved = true;
				continue;
			}
			if (result.empty() || result.back().track != track->second || result.back().dir != dir)
				result.push_back({track->second, dir, result.empty() || referenceIndex > 0 ? std::string() : visit.ocp});
		}
	}
	return result;
}

// Splits each track at the switches and crossings inside it, so that every connection joins piece ends. A track without
// connections belongs to a macroscopic network: it is split at each OCP of its cross-sections where another track ends
// in a macroscopic node or where a train moves from it to another track.
void buildPieces(Context& context) {
	std::vector<std::vector<std::pair<double, std::string>>> splits(context.tracks.size());
	for (const ConnectionElement& connection : context.connections)
		if (connection.place == 0 && connection.orientation != "rightAngled")
			splits[connection.track].emplace_back(connection.pos, "");
	std::set<std::string> nodeOcps;
	for (const Track& track : context.tracks) {
		if (!track.beginConnected && !track.beginOcp.empty())
			nodeOcps.insert(track.beginOcp);
		if (!track.endConnected && !track.endOcp.empty())
			nodeOcps.insert(track.endOcp);
	}
	std::vector<std::set<std::string>> junctions(context.tracks.size());
	for (const TrainPart& part : context.parts) {
		bool unresolved = false;
		const std::vector<TrackRun> runs = partTracks(context, part, unresolved);
		for (std::size_t index = 1; index < runs.size(); ++index)
			if (!runs[index].junction.empty()) {
				junctions[runs[index - 1].track].insert(runs[index].junction);
				junctions[runs[index].track].insert(runs[index].junction);
			}
	}
	for (std::size_t trackIndex = 0; trackIndex < context.tracks.size(); ++trackIndex) {
		const Track& track = context.tracks[trackIndex];
		if (track.hasConnection)
			continue;
		for (const auto& section : track.crossSections)
			if (nodeOcps.count(section.second) > 0 || junctions[trackIndex].count(section.second) > 0)
				splits[trackIndex].push_back(section);
	}
	std::unordered_set<std::string> trackIds;
	for (const Track& track : context.tracks)
		trackIds.insert(track.id);
	for (std::size_t trackIndex = 0; trackIndex < context.tracks.size(); ++trackIndex) {
		Track& track = context.tracks[trackIndex];
		std::vector<std::pair<double, std::string>>& points = splits[trackIndex];
		std::stable_sort(points.begin(), points.end(),
			[](const auto& left, const auto& right) { return left.first < right.first; });
		std::vector<std::pair<double, std::string>> cuts;
		for (const auto& point : points)
			if (point.first > track.beginPos + kMinimumBlockM && point.first < track.endPos - kMinimumBlockM
				&& (cuts.empty() || point.first - cuts.back().first > kMinimumBlockM))
				cuts.push_back(point);
		double start = track.beginPos;
		std::string startOcp = track.beginConnected ? std::string() : track.beginOcp;
		for (std::size_t index = 0; index <= cuts.size(); ++index) {
			Piece piece;
			piece.track = trackIndex;
			piece.beginPos = start;
			piece.beginOcp = startOcp;
			piece.endPos = index < cuts.size() ? cuts[index].first : track.endPos;
			piece.endOcp = index < cuts.size() ? cuts[index].second : (track.endConnected ? std::string() : track.endOcp);
			piece.id = cuts.empty() ? track.id : track.id + ".s" + std::to_string(index + 1);
			while (!cuts.empty() && trackIds.count(piece.id) > 0)
				piece.id += "_";
			track.pieces.push_back(context.pieces.size());
			context.pieces.push_back(piece);
			start = piece.endPos;
			startOcp = piece.endOcp;
		}
	}
	// Each connection element joins one piece end.
	int defaultedOrientation = 0;
	for (ConnectionElement& connection : context.connections) {
		const Track& track = context.tracks[connection.track];
		if (connection.place < 0) {
			connection.end = track.pieces.front() * 2;
			continue;
		}
		if (connection.place > 0) {
			connection.end = track.pieces.back() * 2 + 1;
			continue;
		}
		if (connection.orientation == "rightAngled")
			continue;
		if (connection.orientation != "outgoing" && connection.orientation != "incoming")
			++defaultedOrientation;
		const bool incoming = connection.orientation == "incoming";
		for (const std::size_t pieceIndex : track.pieces) {
			const Piece& piece = context.pieces[pieceIndex];
			if (incoming && std::fabs(piece.beginPos - connection.pos) <= kMinimumBlockM) {
				connection.end = pieceIndex * 2;
				break;
			}
			if (!incoming && std::fabs(piece.endPos - connection.pos) <= kMinimumBlockM) {
				connection.end = pieceIndex * 2 + 1;
				break;
			}
		}
	}
	if (defaultedOrientation > 0) {
		reportDefault(context, "connection",
			std::to_string(defaultedOrientation)
				+ " switch or crossing connections have no orientation incoming or outgoing; they are joined as outgoing, to the track before the switch");
	}
}

// The OCP of a macroscopic node or a junction at a piece end.
const std::string& nodeOcpAt(const Context& context, std::size_t end) {
	const Piece& piece = context.pieces[pieceOf(end)];
	return sideOf(end) == 0 ? piece.beginOcp : piece.endOcp;
}

// The piece end of track `track` at OCP `ocp` on side `side` (0 begin, 1 end); kNone when there is none.
std::size_t pieceEndAt(const Context& context, std::size_t track, const std::string& ocp, int side) {
	for (const std::size_t piece : context.tracks[track].pieces)
		if (nodeOcpAt(context, piece * 2 + static_cast<std::size_t>(side)) == ocp)
			return piece * 2 + static_cast<std::size_t>(side);
	return kNone;
}

void addLink(Context& context, std::size_t endA, std::size_t endB, int priority, const std::string& place,
	std::map<std::pair<std::size_t, std::size_t>, std::size_t>& byEnds) {
	const auto key = std::minmax(endA, endB);
	const auto found = byEnds.find(key);
	if (found != byEnds.end()) {
		context.links[found->second].priority = std::min(context.links[found->second].priority, priority);
		return;
	}
	Link link;
	link.endA = endA;
	link.endB = endB;
	link.priority = priority;
	link.place = place;
	byEnds[key] = context.links.size();
	context.links.push_back(link);
}

void buildLinks(Context& context) {
	std::map<std::pair<std::size_t, std::size_t>, std::size_t> byEnds;
	for (const Track& track : context.tracks)
		for (std::size_t index = 0; index + 1 < track.pieces.size(); ++index)
			addLink(context, track.pieces[index] * 2 + 1, track.pieces[index + 1] * 2, 0, "track " + track.id, byEnds);

	// Microscopic: connection elements that refer to each other.
	std::unordered_map<std::string, std::size_t> connectionById;
	for (std::size_t index = 0; index < context.connections.size(); ++index)
		if (!context.connections[index].id.empty())
			connectionById.emplace(context.connections[index].id, index);
	SceneImportReportRow& connectionRow = reportRow(context, "connection");
	connectionRow.sourceCount += static_cast<int>(context.connections.size());
	std::vector<std::string> unresolvedRefs;
	std::set<std::pair<std::size_t, std::size_t>> pairs;
	for (std::size_t index = 0; index < context.connections.size(); ++index) {
		const ConnectionElement& connection = context.connections[index];
		const auto target = connectionById.find(connection.ref);
		if (target == connectionById.end()) {
			++connectionRow.unresolvedReferences;
			unresolvedRefs.push_back(connection.id + " -> " + connection.ref);
			continue;
		}
		pairs.insert(std::minmax(index, target->second));
	}
	if (!unresolvedRefs.empty())
		addDiagnostic(context, SceneSeverity::Warning, "scene.import.railml.unresolved",
			std::to_string(unresolvedRefs.size()) + " connections refer to a connection that the file does not hold: "
				+ namedList(unresolvedRefs),
			"connection");
	for (const auto& pair : pairs) {
		const ConnectionElement& first = context.connections[pair.first];
		const ConnectionElement& second = context.connections[pair.second];
		if (first.end == kNone || second.end == kNone || first.end == second.end)
			continue;
		const std::size_t before = context.links.size();
		addLink(context, first.end, second.end, 1, first.id + "/" + second.id, byEnds);
		if (context.links.size() > before) {
			context.links.back().elements = {pair.first, pair.second};
			context.links.back().connectionId = std::min(first.id, second.id);
		}
	}

	// Macroscopic: track ends at one OCP. Ends that trains pass from one to the other come first.
	std::map<std::string, std::vector<std::size_t>> endsByOcp;
	for (std::size_t end = 0; end < context.pieces.size() * 2; ++end) {
		const std::string ocp = nodeOcpAt(context, end);
		if (!ocp.empty())
			endsByOcp[ocp].push_back(end);
	}
	std::map<std::pair<std::size_t, std::size_t>, int> movements;
	for (const TrainPart& part : context.parts) {
		bool unresolved = false;
		const std::vector<TrackRun> runs = partTracks(context, part, unresolved);
		for (std::size_t index = 0; index + 1 < runs.size(); ++index) {
			const TrackRun& before = runs[index];
			const TrackRun& after = runs[index + 1];
			std::string ocp = after.junction;
			if (ocp.empty()) {
				// A change of track inside one section: the end of the first track, if the second one starts there.
				const Track& track = context.tracks[before.track];
				ocp = nodeOcpAt(context, before.dir > 0 ? track.pieces.back() * 2 + 1 : track.pieces.front() * 2);
			}
			if (ocp.empty())
				continue;
			const std::size_t from = pieceEndAt(context, before.track, ocp, before.dir > 0 ? 1 : 0);
			const std::size_t to = pieceEndAt(context, after.track, ocp, after.dir > 0 ? 0 : 1);
			if (from != kNone && to != kNone && from != to)
				movements[std::minmax(from, to)] += inPeriod(context, part) ? 1 : 0;
		}
	}
	for (const auto& movement : movements) {
		addLink(context, movement.first.first, movement.first.second, 1, "OCP " + nodeOcpAt(context, movement.first.first), byEnds);
		context.links[byEnds[movement.first]].weight += movement.second;
	}
	for (const auto& entry : endsByOcp) {
		if (!context.parts.empty() && entry.second.size() != 2)
			continue;
		for (std::size_t first = 0; first < entry.second.size(); ++first)
			for (std::size_t second = first + 1; second < entry.second.size(); ++second)
				addLink(context, entry.second[first], entry.second[second], 2, "OCP " + entry.first, byEnds);
	}
	for (const auto& entry : endsByOcp) {
		int ocpLink = 0;
		for (Link& link : context.links)
			if (link.connectionId.empty() && link.priority > 0 && link.place == "OCP " + entry.first)
				link.connectionId = entry.first + "." + std::to_string(++ocpLink);
	}
	for (const Track& track : context.tracks)
		for (std::size_t index = 0; index + 1 < track.pieces.size(); ++index) {
			Link& link = context.links[byEnds[std::minmax(track.pieces[index] * 2 + 1, track.pieces[index + 1] * 2)]];
			if (link.connectionId.empty())
				link.connectionId = context.pieces[track.pieces[index]].id + ".join";
		}
	// Connection ids are unique: a connection element that two pairs share gives its id to the first.
	std::set<std::string> used;
	for (Link& link : context.links) {
		const std::string base = link.connectionId;
		for (int suffix = 2; !used.insert(link.connectionId).second; ++suffix)
			link.connectionId = base + "." + std::to_string(suffix);
	}
}

// Union-find over the pieces with the parity of their orientations.
struct ParityUnion {
	std::vector<std::size_t> parent;
	std::vector<int> parity; // relative to the parent

	explicit ParityUnion(std::size_t count) : parent(count), parity(count, 0) {
		for (std::size_t index = 0; index < count; ++index)
			parent[index] = index;
	}
	std::size_t find(std::size_t item, int& itemParity) {
		itemParity = 0;
		std::size_t root = item;
		while (parent[root] != root) {
			itemParity ^= parity[root];
			root = parent[root];
		}
		// Path compression with the parities kept.
		int remaining = itemParity;
		while (parent[item] != root) {
			const std::size_t next = parent[item];
			const int step = parity[item];
			parent[item] = root;
			parity[item] = remaining;
			remaining ^= step;
			item = next;
		}
		return root;
	}
	// Joins two items with relative parity `relation`; false when they are joined already with the other parity.
	bool unite(std::size_t left, std::size_t right, int relation) {
		int leftParity = 0;
		int rightParity = 0;
		const std::size_t leftRoot = find(left, leftParity);
		const std::size_t rightRoot = find(right, rightParity);
		if (leftRoot == rightRoot)
			return (leftParity ^ rightParity) == relation;
		parent[rightRoot] = leftRoot;
		parity[rightRoot] = leftParity ^ rightParity ^ relation;
		return true;
	}
};

// Single-block pieces have one block for both ends, so the links at both ends count against one block.
bool singleBlock(const Piece& piece) {
	return piece.boundaries.size() <= 2;
}

void layOut(Context& context);

// Orients the pieces so that every accepted link runs from the high chainage end of one piece to the low end of the
// next, and lays the pieces out on the chainage axis. Links that cannot be laid out are reported.
void orientAndLayOut(Context& context) {
	std::vector<std::size_t> order(context.links.size());
	for (std::size_t index = 0; index < order.size(); ++index)
		order[index] = index;
	std::stable_sort(order.begin(), order.end(), [&](std::size_t left, std::size_t right) {
		const Link& a = context.links[left];
		const Link& b = context.links[right];
		return a.priority != b.priority ? a.priority < b.priority : a.weight > b.weight;
	});
	// A train arriving at the end of a piece runs up it; one leaving from the begin of a piece runs up it. Both pieces
	// have to give the train one chainage direction.
	ParityUnion parity(context.pieces.size());
	std::vector<bool> oriented(context.links.size(), false);
	std::map<std::string, std::vector<std::string>> rejected; // reason to places
	for (const std::size_t index : order) {
		const Link& link = context.links[index];
		const int arriving = sideOf(link.endA) == 1 ? 1 : -1;
		const int leaving = sideOf(link.endB) == 0 ? 1 : -1;
		if (parity.unite(pieceOf(link.endA), pieceOf(link.endB), arriving * leaving > 0 ? 0 : 1))
			oriented[index] = true;
		else if (link.priority < 2)
			rejected["the pieces it joins are already joined the other way round"].push_back(link.place);
	}
	// Orientation: in each group the root takes the sign that gives most pieces their railML direction.
	std::map<std::size_t, int> rootBalance;
	std::vector<int> relative(context.pieces.size(), 0);
	std::vector<std::size_t> roots(context.pieces.size(), 0);
	for (std::size_t piece = 0; piece < context.pieces.size(); ++piece) {
		roots[piece] = parity.find(piece, relative[piece]);
		rootBalance[roots[piece]] += relative[piece] == 0 ? 1 : -1;
	}
	for (std::size_t piece = 0; piece < context.pieces.size(); ++piece) {
		const int rootSign = rootBalance[roots[piece]] >= 0 ? 1 : -1;
		context.pieces[piece].sign = relative[piece] == 0 ? rootSign : -rootSign;
	}

	// Accept links in order unless they exceed a capacity of the runtime or close a cycle.
	const std::size_t pieceCount = context.pieces.size();
	std::vector<std::vector<std::size_t>> successors(pieceCount);
	std::vector<std::size_t> endCount(pieceCount * 2, 0);
	std::vector<std::size_t> linksAtPiece(pieceCount, 0);
	context.linksAtEnd.assign(pieceCount * 2, {});
	const auto reaches = [&](std::size_t from, std::size_t target) {
		std::vector<bool> seen(pieceCount, false);
		std::vector<std::size_t> stack{from};
		while (!stack.empty()) {
			const std::size_t current = stack.back();
			stack.pop_back();
			if (current == target)
				return true;
			if (seen[current])
				continue;
			seen[current] = true;
			for (const std::size_t next : successors[current])
				stack.push_back(next);
		}
		return false;
	};
	for (const std::size_t index : order) {
		if (!oriented[index])
			continue;
		Link& link = context.links[index];
		const auto isHigh = [&](std::size_t end) { return (sideOf(end) == 1) == (context.pieces[pieceOf(end)].sign > 0); };
		const std::size_t high = isHigh(link.endA) ? link.endA : link.endB;
		const std::size_t low = high == link.endA ? link.endB : link.endA;
		const std::size_t from = pieceOf(high);
		const std::size_t to = pieceOf(low);
		const auto overCapacity = [&](std::size_t end) {
			const std::size_t piece = pieceOf(end);
			return endCount[end] >= RuntimeLimits::kNativeMaxNodeConnections
				|| (singleBlock(context.pieces[piece]) && linksAtPiece[piece] >= RuntimeLimits::kNativeMaxDependencies);
		};
		if (overCapacity(high) || overCapacity(low)) {
			if (link.priority < 2)
				rejected["a track end would have more connections than the simulation holds ("
					+ std::to_string(RuntimeLimits::kNativeMaxNodeConnections) + ")"]
					.push_back(link.place);
			continue;
		}
		if (from == to || reaches(to, from)) {
			if (link.priority < 2)
				rejected["it closes a loop, which one chainage axis cannot hold"].push_back(link.place);
			continue;
		}
		link.accepted = true;
		link.from = from;
		link.to = to;
		successors[from].push_back(to);
		++endCount[high];
		++endCount[low];
		++linksAtPiece[from];
		++linksAtPiece[to];
		context.linksAtEnd[high].push_back(index);
		context.linksAtEnd[low].push_back(index);
	}
	for (const auto& entry : rejected)
		addDiagnostic(context, SceneSeverity::Warning, "scene.import.railml.topology",
			std::to_string(entry.second.size()) + " track links are left out because " + entry.first + ": "
				+ namedList(entry.second),
			"connection", "", "Trains that need these links are reported as not imported");

	layOut(context);
	if (!context.parts.empty()) {
		// A link that no imported train uses and that the layout lengthens would only add track that no train runs over.
		std::vector<std::string> dropped;
		for (std::size_t index = 0; index < context.links.size(); ++index) {
			Link& link = context.links[index];
			if (!link.accepted || link.priority == 0 || link.weight > 0
				|| context.pieces[link.to].xLow - context.pieces[link.from].xHigh() - kConnectionGapKm <= 0.001)
				continue;
			link.accepted = false;
			dropped.push_back(link.place);
			for (std::vector<std::size_t>& list : context.linksAtEnd)
				list.erase(std::remove(list.begin(), list.end(), index), list.end());
		}
		if (!dropped.empty()) {
			addDiagnostic(context, SceneSeverity::Info, "scene.import.railml.topology",
				std::to_string(dropped.size())
					+ " track links that no imported train uses are left out, because the network has paths of different "
					  "lengths between the same points, which one chainage axis cannot hold, and they would be longer than 1 m: "
					+ namedList(dropped),
				"connection");
			layOut(context);
		}
	}
	double totalSlack = 0.0;
	double longest = 0.0;
	std::string longestPlace;
	int slackLinks = 0;
	for (const Link& link : context.links) {
		if (!link.accepted)
			continue;
		const double slack = context.pieces[link.to].xLow - context.pieces[link.from].xHigh() - kConnectionGapKm;
		if (slack > 0.001) {
			++slackLinks;
			totalSlack += slack;
			if (slack > longest) {
				longest = slack;
				longestPlace = link.place;
			}
		}
	}
	if (slackLinks > 0)
		addDiagnostic(context, SceneSeverity::Warning, "scene.import.railml.connection_length",
			std::to_string(slackLinks)
				+ " connections are longer than 1 m because the network has paths of different lengths between the same points, "
				  "which one chainage axis cannot hold: "
				+ numberText(round6(totalSlack)) + " km in all, the longest " + numberText(round6(longest)) + " km at "
				+ longestPlace + ". Trains over them run that much further",
			"connection");
}


// The chainage of each piece: the longest path from the pieces without predecessors, then each piece moves to the side
// whose links carry more train movements, so that the slack of the layout falls on the links that fewer trains use.
void layOut(Context& context) {
	const std::size_t pieceCount = context.pieces.size();
	std::vector<std::vector<std::size_t>> incoming(pieceCount);
	std::vector<std::vector<std::size_t>> outgoing(pieceCount);
	for (std::size_t index = 0; index < context.links.size(); ++index) {
		const Link& link = context.links[index];
		if (!link.accepted)
			continue;
		outgoing[link.from].push_back(index);
		incoming[link.to].push_back(index);
	}
	std::vector<std::size_t> indegree(pieceCount, 0);
	for (std::size_t piece = 0; piece < pieceCount; ++piece)
		indegree[piece] = incoming[piece].size();
	std::vector<std::size_t> topological;
	std::set<std::size_t> ready;
	for (std::size_t piece = 0; piece < pieceCount; ++piece)
		if (indegree[piece] == 0)
			ready.insert(piece);
	while (!ready.empty()) {
		const std::size_t piece = *ready.begin();
		ready.erase(ready.begin());
		topological.push_back(piece);
		for (const std::size_t index : outgoing[piece])
			if (--indegree[context.links[index].to] == 0)
				ready.insert(context.links[index].to);
	}
	const auto weightOf = [&](std::size_t index) {
		const Link& link = context.links[index];
		return link.priority == 0 ? 1e6 : 1.0 + link.weight;
	};
	std::vector<double> x(pieceCount, 0.0);
	for (const std::size_t piece : topological)
		for (const std::size_t index : incoming[piece]) {
			const std::size_t from = context.links[index].from;
			x[piece] = std::max(x[piece], x[from] + context.pieces[from].lengthKm() + kConnectionGapKm);
		}
	for (int iteration = 0; iteration < 50; ++iteration) {
		bool changed = false;
		for (auto it = topological.rbegin(); it != topological.rend(); ++it) {
			const std::size_t piece = *it;
			if (outgoing[piece].empty())
				continue;
			double inWeight = 0.0;
			double outWeight = 0.0;
			double high = std::numeric_limits<double>::infinity();
			for (const std::size_t index : incoming[piece])
				inWeight += weightOf(index);
			for (const std::size_t index : outgoing[piece]) {
				outWeight += weightOf(index);
				high = std::min(high, x[context.links[index].to] - context.pieces[piece].lengthKm() - kConnectionGapKm);
			}
			if ((incoming[piece].empty() || outWeight > inWeight) && high > x[piece] + 1e-9) {
				x[piece] = high;
				changed = true;
			}
		}
		for (const std::size_t piece : topological) {
			if (incoming[piece].empty())
				continue;
			double inWeight = 0.0;
			double outWeight = 0.0;
			double low = 0.0;
			for (const std::size_t index : outgoing[piece])
				outWeight += weightOf(index);
			for (const std::size_t index : incoming[piece]) {
				inWeight += weightOf(index);
				const std::size_t from = context.links[index].from;
				low = std::max(low, x[from] + context.pieces[from].lengthKm() + kConnectionGapKm);
			}
			if ((outgoing[piece].empty() || inWeight > outWeight) && low < x[piece] - 1e-9) {
				x[piece] = low;
				changed = true;
			}
		}
		if (!changed)
			break;
	}
	double lowest = std::numeric_limits<double>::infinity();
	for (const double value : x)
		lowest = std::min(lowest, value);
	for (std::size_t piece = 0; piece < pieceCount; ++piece)
		context.pieces[piece].xLow = round6(x[piece] - (std::isfinite(lowest) ? lowest : 0.0));
}

// --- Infrastructure ---

// Breakpoint ranks: 4 piece ends and block boundaries, 3 OCPs and platforms, 2 speed, 1 gradient, 0 radius.
struct Breakpoint {
	double pos = 0.0;
	int rank = 0;
};

struct Arc {
	double low = 0.0;
	double high = 0.0;
	double speedKmh = 0.0;
	double gradient = 0.0;	// rise per length up the railML positions
	double curvature = 0.0; // 1 / radius in 1/m
	double length() const { return high - low; }
};

// The value of up changes at a position: the last change at or before it; down changes: the first change after it.
struct DirectedProfile {
	std::vector<std::pair<double, std::optional<double>>> up;	// ascending position
	std::vector<std::pair<double, std::optional<double>>> down; // descending position

	void build(std::vector<const Change*> changes) {
		std::stable_sort(changes.begin(), changes.end(), [](const Change* a, const Change* b) { return a->pos < b->pos; });
		std::vector<double> stack;
		for (const Change* change : changes) {
			if (change->dir < 0)
				continue;
			if (change->end) {
				if (!stack.empty())
					stack.pop_back();
			} else
				stack.push_back(change->value);
			up.emplace_back(change->pos, stack.empty() ? std::nullopt : std::optional<double>(stack.back()));
		}
		stack.clear();
		for (auto it = changes.rbegin(); it != changes.rend(); ++it) {
			const Change* change = *it;
			if (change->dir >= 0)
				continue;
			if (change->end) {
				if (!stack.empty())
					stack.pop_back();
			} else
				stack.push_back(change->value);
			down.emplace_back(change->pos, stack.empty() ? std::nullopt : std::optional<double>(stack.back()));
		}
	}
	std::optional<double> upAt(double pos) const {
		std::optional<double> value;
		for (const auto& entry : up) {
			if (entry.first > pos)
				break;
			value = entry.second;
		}
		return value;
	}
	std::optional<double> downAt(double pos) const {
		std::optional<double> value;
		for (const auto& entry : down) {
			if (entry.first < pos)
				break;
			value = entry.second;
		}
		return value;
	}
};

// The speed changes of a track that apply: those without a profile, else those of the profile that the most train
// sequences use, else those of the profile with the most changes on the track.
std::vector<const Change*> chosenSpeedChanges(const Context& context, const Track& track, int& skipped) {
	std::map<std::string, std::vector<const Change*>> byProfile;
	for (const Change& change : track.speed)
		byProfile[change.profile].push_back(&change);
	std::string chosen;
	if (byProfile.count("") > 0)
		chosen = "";
	else if (byProfile.count(context.preferredSpeedProfile) > 0)
		chosen = context.preferredSpeedProfile;
	else {
		std::size_t most = 0;
		for (const auto& entry : byProfile)
			if (entry.second.size() > most) {
				most = entry.second.size();
				chosen = entry.first;
			}
	}
	skipped = 0;
	for (const auto& entry : byProfile)
		if (entry.first != chosen)
			skipped += static_cast<int>(entry.second.size());
	const auto found = byProfile.find(chosen);
	return found == byProfile.end() ? std::vector<const Change*>() : found->second;
}

void buildBlocks(Context& context) {
	for (const Track& track : context.tracks)
		for (const Signal& signal : track.signals)
			if (signal.type == "main" || signal.type == "combined")
				context.signalBlocks = true;
	for (Piece& piece : context.pieces) {
		const Track& track = context.tracks[piece.track];
		std::vector<double> inner;
		if (context.signalBlocks) {
			for (const Signal& signal : track.signals)
				if (signal.type == "main" || signal.type == "combined")
					inner.push_back(signal.pos);
		} else {
			for (const auto& section : track.crossSections)
				inner.push_back(section.first);
		}
		std::sort(inner.begin(), inner.end());
		piece.boundaries = {piece.beginPos};
		for (const double pos : inner)
			if (pos > piece.beginPos + kMinimumBlockM && pos < piece.endPos - kMinimumBlockM
				&& pos > piece.boundaries.back() + kMinimumBlockM)
				piece.boundaries.push_back(pos);
		piece.boundaries.push_back(piece.endPos);
	}
}

// The positions of each OCP on each track: cross-sections and macroscopic nodes, and the stop positions of the
// platform edges assigned to it. Platform edges go to the OCP named by their ocpRef, else to the OCP of the nearest
// cross-section on their track.
void placeOcps(Context& context) {
	SceneImportReportRow& edgeRow = reportRow(context, "platformEdge");
	std::vector<std::string> unassigned;
	for (std::size_t trackIndex = 0; trackIndex < context.tracks.size(); ++trackIndex) {
		Track& track = context.tracks[trackIndex];
		for (const auto& section : track.crossSections)
			context.ocpPositions[{trackIndex, section.second}].push_back(section.first);
		if (!track.beginOcp.empty() && !track.beginConnected)
			context.ocpPositions[{trackIndex, track.beginOcp}].push_back(track.beginPos);
		if (!track.endOcp.empty() && !track.endConnected)
			context.ocpPositions[{trackIndex, track.endOcp}].push_back(track.endPos);
		for (PlatformEdge& edge : track.platforms) {
			const double halfLength = edge.hasLength ? edge.length / 2.0 : 0.0;
			edge.stopPos = std::clamp(edge.pos + halfLength, track.beginPos, track.endPos);
			if (!edge.ocp.empty() && context.ocpIndex.count(edge.ocp) > 0)
				edge.stationOcp = edge.ocp;
			else {
				double nearest = std::numeric_limits<double>::infinity();
				for (const auto& section : track.crossSections)
					if (std::fabs(section.first - edge.stopPos) < nearest) {
						nearest = std::fabs(section.first - edge.stopPos);
						edge.stationOcp = section.second;
					}
			}
			if (edge.stationOcp.empty() || context.ocpIndex.count(edge.stationOcp) == 0) {
				edge.stationOcp.clear();
				++edgeRow.skippedCount;
				unassigned.push_back(edge.id);
				continue;
			}
			context.ocpPositions[{trackIndex, edge.stationOcp}].push_back(edge.stopPos);
		}
	}
	for (auto& entry : context.ocpPositions)
		std::sort(entry.second.begin(), entry.second.end());
	if (!unassigned.empty())
		addDiagnostic(context, SceneSeverity::Warning, "scene.import.railml.skipped",
			std::to_string(unassigned.size())
				+ " platform edges are skipped: they have no ocpRef and their track has no cross-section of an OCP: "
				+ namedList(unassigned),
			"platformEdge");
}

std::string nodeAt(const Piece& piece, double pos) {
	std::size_t best = kNone;
	double distance = std::numeric_limits<double>::infinity();
	for (std::size_t index = 0; index < piece.nodePos.size(); ++index)
		if (std::fabs(piece.nodePos[index] - pos) < distance) {
			distance = std::fabs(piece.nodePos[index] - pos);
			best = index;
		}
	return best == kNone || distance > kSamePositionM + 1e-9 ? std::string() : piece.nodeIds[best];
}

// Merges the arcs of one block, between positions low and high, until it has at most `limit` arcs. Speed changes are
// merged last; the merged arc takes the lower speed, the length-weighted gradient and the length-weighted curvature.
void mergeArcs(std::vector<Arc>& arcs, std::vector<int>& ranks, std::size_t first, std::size_t& last, std::size_t limit,
	std::map<int, int>& merged) {
	while (last - first > limit) {
		std::size_t best = kNone;
		double bestCost = std::numeric_limits<double>::infinity();
		for (std::size_t index = first + 1; index < last; ++index) {
			if (ranks[index] >= 3)
				continue;
			const Arc& left = arcs[index - 1];
			const Arc& right = arcs[index];
			const double weight = left.length() * right.length() / (left.length() + right.length());
			double cost = (std::fabs(left.gradient - right.gradient) + std::fabs(left.curvature - right.curvature)) * weight;
			if (left.speedKmh != right.speedKmh)
				cost += 1e9 + std::fabs(left.speedKmh - right.speedKmh) * weight;
			if (cost < bestCost) {
				bestCost = cost;
				best = index;
			}
		}
		if (best == kNone)
			return;
		Arc& left = arcs[best - 1];
		const Arc& right = arcs[best];
		const double total = left.length() + right.length();
		left.gradient = (left.gradient * left.length() + right.gradient * right.length()) / total;
		left.curvature = (left.curvature * left.length() + right.curvature * right.length()) / total;
		left.speedKmh = std::min(left.speedKmh, right.speedKmh);
		left.high = right.high;
		++merged[ranks[best]];
		arcs.erase(arcs.begin() + static_cast<std::ptrdiff_t>(best));
		ranks.erase(ranks.begin() + static_cast<std::ptrdiff_t>(best));
		--last;
	}
}

void buildInfrastructure(Context& context) {
	SceneModel& scene = context.scene;
	int defaultSpeedArcs = 0;
	double defaultSpeedKm = 0.0;
	int flatPieces = 0;
	int straightPieces = 0;
	int skippedSpeedChanges = 0;
	std::map<int, int> merged;
	std::vector<std::string> overfull;
	SceneImportReportRow& speedRow = reportRow(context, "speedChange");
	SceneImportReportRow& gradientRow = reportRow(context, "gradientChange");
	SceneImportReportRow& radiusRow = reportRow(context, "radiusChange");
	std::vector<std::vector<const Change*>> speedByTrack(context.tracks.size());
	for (std::size_t trackIndex = 0; trackIndex < context.tracks.size(); ++trackIndex) {
		int skipped = 0;
		speedByTrack[trackIndex] = chosenSpeedChanges(context, context.tracks[trackIndex], skipped);
		skippedSpeedChanges += skipped;
		speedRow.convertedCount += static_cast<int>(speedByTrack[trackIndex].size());
		speedRow.skippedCount += skipped;
		gradientRow.convertedCount += static_cast<int>(context.tracks[trackIndex].gradient.size());
		radiusRow.convertedCount += static_cast<int>(context.tracks[trackIndex].radius.size());
	}
	if (skippedSpeedChanges > 0)
		addDiagnostic(context, SceneSeverity::Info, "scene.import.railml.unsupported",
			std::to_string(skippedSpeedChanges)
				+ " speed changes of other speed profiles are not used: a scene arc has one speed limit. Each track uses its "
				  "changes without a profile, else those of profile "
				+ (context.preferredSpeedProfile.empty() ? std::string("(none)") : context.preferredSpeedProfile)
				+ ", which the most train sequences use, else the profile with the most changes on it",
			"speedChange");

	for (Piece& piece : context.pieces) {
		const Track& track = context.tracks[piece.track];
		scene.tracks.push_back({piece.id});
		// Breakpoints.
		std::vector<Breakpoint> points;
		for (const double boundary : piece.boundaries)
			points.push_back({boundary, 4});
		const auto inside = [&](double pos) { return pos > piece.beginPos && pos < piece.endPos; };
		for (const auto& entry : context.ocpPositions)
			if (entry.first.first == piece.track)
				for (const double pos : entry.second)
					if (inside(pos))
						points.push_back({pos, 3});
		for (const Change* change : speedByTrack[piece.track])
			if (inside(change->pos))
				points.push_back({change->pos, 2});
		for (const Change& change : track.gradient)
			if (inside(change.pos))
				points.push_back({change.pos, 1});
		for (const Change& change : track.radius)
			if (inside(change.pos))
				points.push_back({change.pos, 0});
		std::stable_sort(points.begin(), points.end(), [](const Breakpoint& a, const Breakpoint& b) { return a.pos < b.pos; });
		std::vector<Breakpoint> unique;
		for (const Breakpoint& point : points) {
			if (!unique.empty() && point.pos - unique.back().pos <= kSamePositionM) {
				if (point.rank > unique.back().rank)
					unique.back() = point;
				continue;
			}
			unique.push_back(point);
		}
		// Profiles.
		DirectedProfile speed;
		speed.build(speedByTrack[piece.track]);
		std::vector<const Change*> gradients;
		for (const Change& change : track.gradient)
			gradients.push_back(&change);
		DirectedProfile gradient;
		gradient.build(gradients);
		std::vector<const Change*> radii;
		for (const Change& change : track.radius)
			radii.push_back(&change);
		DirectedProfile radius;
		radius.build(radii);
		if (track.gradient.empty())
			++flatPieces;
		if (track.radius.empty())
			++straightPieces;

		std::vector<Arc> arcs;
		std::vector<int> ranks; // rank of the breakpoint at the start of each arc
		for (std::size_t index = 0; index + 1 < unique.size(); ++index) {
			Arc arc;
			arc.low = unique[index].pos;
			arc.high = unique[index + 1].pos;
			const double middle = (arc.low + arc.high) / 2.0;
			const auto up = speed.upAt(middle);
			const auto down = speed.downAt(middle);
			if (up && down)
				arc.speedKmh = std::min(*up, *down);
			else if (up || down)
				arc.speedKmh = up ? *up : *down;
			else {
				arc.speedKmh = kDefaultSpeedLimitKmh;
				++defaultSpeedArcs;
				defaultSpeedKm += arc.length() / 1000.0;
			}
			const auto gradientUp = gradient.upAt(middle);
			const auto gradientDown = gradient.downAt(middle);
			arc.gradient = gradientUp ? *gradientUp / 1000.0 : (gradientDown ? -*gradientDown / 1000.0 : 0.0);
			const auto radiusUp = radius.upAt(middle);
			const auto radiusDown = radius.downAt(middle);
			const double radiusM = std::fabs(radiusUp ? *radiusUp : radiusDown.value_or(0.0));
			arc.curvature = radiusM > 0.0 ? 1.0 / radiusM : 0.0;
			arcs.push_back(arc);
			ranks.push_back(unique[index].rank);
		}
		// Each block within its capacity.
		std::size_t first = 0;
		const std::size_t blockCount = piece.boundaries.size() - 1;
		for (std::size_t block = 0; block < blockCount; ++block) {
			std::size_t last = first;
			while (last < arcs.size() && arcs[last].high <= piece.boundaries[block + 1] + 1e-9)
				++last;
			const std::size_t limit = block == 0 || block + 1 == blockCount ? kEndBlockArcs : kInteriorBlockArcs;
			mergeArcs(arcs, ranks, first, last, limit, merged);
			if (last - first > limit)
				overfull.push_back(piece.id);
			first = last;
		}
		// Nodes, arcs and blocks in chainage order.
		piece.nodePos.clear();
		for (const Arc& arc : arcs)
			piece.nodePos.push_back(arc.low);
		piece.nodePos.push_back(piece.endPos);
		const std::size_t nodeCount = piece.nodePos.size();
		piece.nodeIds.assign(nodeCount, "");
		for (std::size_t k = 0; k < nodeCount; ++k) {
			const std::size_t index = piece.sign > 0 ? k : nodeCount - 1 - k;
			piece.nodeIds[index] = piece.id + ".n" + std::to_string(k + 1);
			scene.nodes.push_back({piece.nodeIds[index], piece.id, piece.x(piece.nodePos[index]), 0.0});
		}
		for (std::size_t k = 0; k < arcs.size(); ++k) {
			const std::size_t index = piece.sign > 0 ? k : arcs.size() - 1 - k;
			const Arc& arc = arcs[index];
			SceneArc sceneArc;
			sceneArc.id = piece.id + ".a" + std::to_string(k + 1);
			sceneArc.trackId = piece.id;
			sceneArc.fromNodeId = piece.sign > 0 ? piece.nodeIds[index] : piece.nodeIds[index + 1];
			sceneArc.toNodeId = piece.sign > 0 ? piece.nodeIds[index + 1] : piece.nodeIds[index];
			sceneArc.curvatureRadiusM = arc.curvature > 0.0 ? std::round(1.0 / arc.curvature) : 0.0;
			sceneArc.gradientPercent = piece.sign * round6(arc.gradient);
			if (sceneArc.gradientPercent == 0.0)
				sceneArc.gradientPercent = 0.0;
			sceneArc.speedLimitMs = arc.speedKmh / 3.6;
			scene.arcs.push_back(sceneArc);
		}
		piece.blockIds.assign(blockCount, "");
		for (std::size_t k = 0; k < blockCount; ++k) {
			const std::size_t index = piece.sign > 0 ? k : blockCount - 1 - k;
			piece.blockIds[index] = piece.id + ".b" + std::to_string(k + 1);
			const double length = round6(std::fabs(piece.x(piece.boundaries[index + 1]) - piece.x(piece.boundaries[index])));
			scene.blocks.push_back({piece.blockIds[index], piece.id, length});
		}
	}

	for (Link& link : context.links) {
		if (!link.accepted)
			continue;
		const Piece& from = context.pieces[link.from];
		const Piece& to = context.pieces[link.to];
		SceneConnection connection;
		connection.id = link.connectionId;
		connection.fromNodeId = from.sign > 0 ? from.nodeIds.back() : from.nodeIds.front();
		connection.toNodeId = to.sign > 0 ? to.nodeIds.front() : to.nodeIds.back();
		scene.connections.push_back(connection);
		reportRow(context, "connection").convertedCount += static_cast<int>(link.elements.size());
	}
	SceneImportReportRow& connectionRow = reportRow(context, "connection");
	connectionRow.skippedCount = connectionRow.sourceCount - connectionRow.convertedCount - connectionRow.unresolvedReferences;
	// A switch or crossing counts as converted when one of its connections became a scene connection.
	std::map<bool, std::set<std::string>> usedOwners;
	for (const Link& link : context.links)
		if (link.accepted)
			for (const std::size_t element : link.elements)
				if (!context.connections[element].owner.empty())
					usedOwners[context.connections[element].inCrossing].insert(context.connections[element].owner);
	for (const bool crossing : {false, true}) {
		SceneImportReportRow& row = reportRow(context, crossing ? "crossing" : "switch");
		row.convertedCount = static_cast<int>(usedOwners[crossing].size());
		row.skippedCount = row.sourceCount - row.convertedCount;
	}

	if (defaultSpeedArcs > 0) {
		reportDefault(context, "speedChange",
			std::to_string(defaultSpeedArcs) + " arcs (" + numberText(round6(defaultSpeedKm))
				+ " km) are covered by no speed change; they get the default speed limit of "
				+ numberText(kDefaultSpeedLimitKmh) + " km/h");
	}
	if (flatPieces > 0) {
		reportDefault(context, "gradientChange",
			std::to_string(flatPieces) + " tracks have no gradient changes; they are flat");
	}
	if (straightPieces > 0) {
		reportDefault(context, "radiusChange",
			std::to_string(straightPieces) + " tracks have no radius changes; they are straight");
	}
	int mergedCount = 0;
	for (const auto& entry : merged)
		mergedCount += entry.second;
	if (mergedCount > 0)
		addDiagnostic(context, SceneSeverity::Info, "scene.import.railml.profile_merged",
			std::to_string(mergedCount) + " speed, gradient and radius changes (" + std::to_string(merged[2])
				+ " of them speed changes) are merged into the arcs beside them, so that each block holds at most "
				+ std::to_string(kInteriorBlockArcs) + " arcs, and a block at a track end at most "
				+ std::to_string(kEndBlockArcs) + ". A merged arc has the lower speed limit and the length-weighted mean "
												  "gradient and curvature",
			"arc");
	if (!overfull.empty())
		addDiagnostic(context, SceneSeverity::Warning, "scene.import.railml.capacity",
			"Blocks of " + std::to_string(overfull.size())
				+ " tracks hold more OCP and platform nodes than a section holds arcs: " + namedList(overfull),
			"block");
}

void buildStations(Context& context) {
	SceneModel& scene = context.scene;
	SceneImportReportRow& ocpRow = reportRow(context, "ocp");
	SceneImportReportRow& sectionRow = reportRow(context, "crossSection");
	SceneImportReportRow& edgeRow = reportRow(context, "platformEdge");
	SceneImportReportRow& nodeRow = reportRow(context, "macroscopicNode");
	std::vector<std::string> conflicts;
	std::vector<std::string> unknownOcps;
	std::map<std::string, std::vector<ScenePlatform>> platformsByOcp;
	int defaultLength = 0;
	// The pieces of a track at a position: one, or the two pieces that meet there where the track is split.
	const auto piecesAt = [&](std::size_t trackIndex, double pos) {
		std::vector<std::size_t> pieces;
		for (const std::size_t index : context.tracks[trackIndex].pieces) {
			const Piece& piece = context.pieces[index];
			if (pos >= piece.beginPos - kSamePositionM && pos <= piece.endPos + kSamePositionM
				&& !nodeAt(piece, pos).empty())
				pieces.push_back(index);
		}
		return pieces;
	};
	const auto nodesAt = [&](const std::vector<std::size_t>& pieces, double pos) {
		std::vector<std::string> nodes;
		for (const std::size_t index : pieces)
			nodes.push_back(nodeAt(context.pieces[index], pos));
		return nodes;
	};
	const auto claim = [&](const std::vector<std::string>& nodeIds, const std::string& owner) {
		if (nodeIds.empty())
			return false;
		for (const std::string& nodeId : nodeIds)
			if (context.nodeOwner.count(nodeId) > 0) {
				conflicts.push_back(owner);
				return false;
			}
		for (const std::string& nodeId : nodeIds)
			context.nodeOwner.emplace(nodeId, owner);
		return true;
	};
	const auto addPlatform = [&](const std::string& ocp, ScenePlatform platform) {
		std::vector<ScenePlatform>& list = platformsByOcp[ocp];
		const std::string base = platform.id;
		for (int suffix = 2; std::any_of(list.begin(), list.end(), [&](const ScenePlatform& p) { return p.id == platform.id; }); ++suffix)
			platform.id = base + "." + std::to_string(suffix);
		list.push_back(std::move(platform));
	};
	for (std::size_t trackIndex = 0; trackIndex < context.tracks.size(); ++trackIndex) {
		const Track& track = context.tracks[trackIndex];
		std::set<std::string> ocpsWithEdges;
		for (const PlatformEdge& edge : track.platforms) {
			if (edge.stationOcp.empty())
				continue;
			const std::vector<std::string> nodeIds = nodesAt(piecesAt(trackIndex, edge.stopPos), edge.stopPos);
			if (!claim(nodeIds, edge.id)) {
				++edgeRow.skippedCount;
				continue;
			}
			ScenePlatform platform;
			platform.id = edge.id;
			platform.nodeIds = nodeIds;
			platform.hasLength = edge.hasLength;
			platform.lengthM = edge.hasLength ? edge.length : kDefaultPlatformLengthM;
			if (!edge.hasLength)
				++defaultLength;
			addPlatform(edge.stationOcp, platform);
			ocpsWithEdges.insert(edge.stationOcp);
			++edgeRow.convertedCount;
		}
		std::vector<std::pair<double, std::string>> points = track.crossSections;
		const std::size_t sections = points.size();
		if (!track.beginOcp.empty() && !track.beginConnected)
			points.emplace_back(track.beginPos, track.beginOcp);
		if (!track.endOcp.empty() && !track.endConnected)
			points.emplace_back(track.endPos, track.endOcp);
		for (std::size_t index = 0; index < points.size(); ++index) {
			const bool isSection = index < sections;
			SceneImportReportRow& row = isSection ? sectionRow : nodeRow;
			const std::string& ocp = points[index].second;
			if (context.ocpIndex.count(ocp) == 0) {
				++row.unresolvedReferences;
				unknownOcps.push_back(ocp);
				continue;
			}
			if (ocpsWithEdges.count(ocp) > 0) {
				++row.convertedCount; // the platform edges of the OCP on this track stand for it
				continue;
			}
			const std::vector<std::size_t> pieces = piecesAt(trackIndex, points[index].first);
			const std::vector<std::string> nodeIds = nodesAt(pieces, points[index].first);
			if (!claim(nodeIds, ocp + " on " + track.id)) {
				++row.skippedCount;
				continue;
			}
			ScenePlatform platform;
			platform.id = pieces.size() == 1 ? context.pieces[pieces.front()].id : track.id;
			platform.nodeIds = nodeIds;
			++defaultLength;
			addPlatform(ocp, platform);
			++row.convertedCount;
		}
	}
	for (const Ocp& ocp : context.ocps) {
		const auto found = platformsByOcp.find(ocp.id);
		if (found == platformsByOcp.end()) {
			++ocpRow.skippedCount;
			continue;
		}
		SceneStation station;
		station.id = ocp.id;
		station.name = ocp.name;
		station.platforms = found->second;
		scene.stations.push_back(station);
		++ocpRow.convertedCount;
	}
	if (ocpRow.skippedCount > 0)
		addDiagnostic(context, SceneSeverity::Info, "scene.import.railml.skipped",
			std::to_string(ocpRow.skippedCount)
				+ " OCPs lie on no track (no cross-section, macroscopic node or platform edge), so they do not become stations",
			"ocp");
	if (!unknownOcps.empty())
		addDiagnostic(context, SceneSeverity::Warning, "scene.import.railml.unresolved",
			std::to_string(unknownOcps.size()) + " cross-sections and macroscopic nodes refer to an OCP that the file does not hold: "
				+ namedList(unknownOcps),
			"ocp");
	if (!conflicts.empty())
		addDiagnostic(context, SceneSeverity::Warning, "scene.import.railml.skipped",
			std::to_string(conflicts.size())
				+ " platforms are skipped because another platform already uses their node (closer than 0.5 m): "
				+ namedList(conflicts),
			"platform");
	if (defaultLength > 0) {
		reportDefault(context, "platform",
			std::to_string(defaultLength) + " platforms have no platform edge length; they get the default length of "
				+ numberText(kDefaultPlatformLengthM) + " m");
	}
}

// The block of a piece that holds a position, on the side of direction dir when the position is a boundary.
std::size_t blockAt(const Piece& piece, double pos, int dir) {
	const std::size_t count = piece.blockIds.size();
	for (std::size_t index = 0; index < count; ++index) {
		const double low = piece.boundaries[index];
		const double high = piece.boundaries[index + 1];
		if (std::fabs(pos - low) <= kSamePositionM)
			return dir >= 0 || index == 0 ? index : index - 1;
		if (pos > low && pos < high - kSamePositionM)
			return index;
	}
	return count - 1;
}

void buildSignals(Context& context) {
	SceneModel& scene = context.scene;
	SceneImportReportRow& row = reportRow(context, "signal");
	std::map<std::string, std::vector<std::string>> skipped; // kind to ids
	if (context.signalBlocks) {
		for (std::size_t trackIndex = 0; trackIndex < context.tracks.size(); ++trackIndex) {
			const Track& track = context.tracks[trackIndex];
			for (const Signal& signal : track.signals) {
				if (signal.type != "main" && signal.type != "combined") {
					skipped[signal.type.empty() ? std::string("without type") : signal.type].push_back(signal.id);
					++row.skippedCount;
					continue;
				}
				std::size_t pieceIndex = kNone;
				for (const std::size_t index : track.pieces) {
					const Piece& piece = context.pieces[index];
					if (signal.pos >= piece.beginPos - kSamePositionM && signal.pos <= piece.endPos + kSamePositionM
						&& (pieceIndex == kNone || signal.dir >= 0))
						pieceIndex = index;
				}
				if (pieceIndex == kNone) {
					skipped["outside its track"].push_back(signal.id);
					++row.skippedCount;
					continue;
				}
				const Piece& piece = context.pieces[pieceIndex];
				scene.signals.push_back({signal.id, piece.blockIds[blockAt(piece, signal.pos, signal.dir)]});
				++row.convertedCount;
			}
		}
		for (const auto& entry : skipped)
			addDiagnostic(context, SceneSeverity::Info, "scene.import.railml.unsupported",
				std::to_string(entry.second.size()) + " signals " + (entry.first == "without type" ? "without a type" : "of type " + entry.first)
					+ " are not converted: only main and combined signals start a block. " + namedList(entry.second),
				"signal");
	} else {
		for (const Piece& piece : context.pieces)
			for (const std::string& block : piece.blockIds) {
				scene.signals.push_back({block + ".up", block});
				scene.signals.push_back({block + ".down", block});
			}
		for (const Track& track : context.tracks)
			if (!track.signals.empty())
				row.skippedCount += static_cast<int>(track.signals.size());
		addDiagnostic(context, SceneSeverity::Info, "scene.import.railml.signals_generated",
			"Signals generated: the file has no main signals. " + std::to_string(scene.signals.size())
				+ " signals are generated, one at each end of each block for each direction, and the blocks run between "
				  "the OCPs of each track (fixed block)",
			"signal");
	}
	// One signalling level for the whole network.
	double highest = 0.0;
	for (const Piece& piece : context.pieces)
		highest = std::max(highest, piece.xHigh());
	if (!context.pieces.empty()) {
		SceneSignallingArea area;
		area.id = "network";
		area.startKm = 0.0;
		area.endKm = std::ceil(highest) + 1.0;
		area.level = 0;
		scene.signallingAreas.push_back(area);
		reportDefault(context, "signalling",
			"railML 2 has no signalling system for a track; the whole network gets signalling level 0, fixed block with "
			"lineside signals, as the bundled cases have");
	}
}

void buildViews(Context& context) {
	SceneModel& scene = context.scene;
	// Geographic when every track has two stations with coordinates at different chainages.
	std::vector<std::vector<std::pair<double, std::string>>> anchors(context.pieces.size());
	std::unordered_map<std::string, std::pair<std::size_t, double>> nodePlace; // piece and chainage
	for (std::size_t index = 0; index < context.pieces.size(); ++index) {
		const Piece& piece = context.pieces[index];
		for (std::size_t node = 0; node < piece.nodeIds.size(); ++node)
			nodePlace[piece.nodeIds[node]] = {index, piece.x(piece.nodePos[node])};
	}
	for (const SceneStation& station : scene.stations) {
		const Ocp& ocp = context.ocps[context.ocpIndex.at(station.id)];
		if (!ocp.hasGeo)
			continue;
		for (const ScenePlatform& platform : station.platforms) {
			const auto place = nodePlace.find(platform.nodeIds.front());
			if (place != nodePlace.end())
				anchors[place->second.first].emplace_back(place->second.second, station.id);
		}
	}
	std::size_t placed = 0;
	for (auto& list : anchors) {
		std::sort(list.begin(), list.end());
		if (!list.empty() && list.back().first - list.front().first > 1e-6)
			++placed;
	}
	int located = 0;
	for (const Ocp& ocp : context.ocps)
		located += ocp.hasGeo ? 1 : 0;
	reportRow(context, "geoCoord").convertedCount = 0;
	const bool geographic = !context.pieces.empty() && placed == context.pieces.size();
	if (geographic) {
		std::map<std::pair<std::string, std::string>, int> parallel;
		std::map<std::string, std::vector<std::pair<int, double>>> regions;
		for (std::size_t index = 0; index < context.pieces.size(); ++index) {
			Piece& piece = context.pieces[index];
			piece.region = static_cast<int>(index) + 1;
			const auto ends = std::minmax(anchors[index].front().second, anchors[index].back().second);
			piece.level = parallel[ends]++;
			for (const auto& anchor : anchors[index])
				regions[anchor.second].emplace_back(piece.region, anchor.first);
		}
		for (const auto& entry : regions) {
			const Ocp& ocp = context.ocps[context.ocpIndex.at(entry.first)];
			SceneStationView view;
			view.stationId = entry.first;
			view.latitude = ocp.latitude;
			view.longitude = ocp.longitude;
			view.regions = entry.second;
			scene.stationViews.push_back(view);
		}
		reportRow(context, "geoCoord").convertedCount = static_cast<int>(regions.size());
		addDiagnostic(context, SceneSeverity::Info, "scene.import.railml.layout",
			"Geographic layout: the stations take the latitude and longitude of their OCP (WGS 84, EPSG:4326, read as "
			"latitude then longitude), and the preview draws each track between its stations",
			"view");
	} else {
		// Schematic: chainage across, and a track that overlaps another one in chainage on another level. A track keeps the
		// level of the track before it where it can.
		std::vector<std::size_t> order(context.pieces.size());
		for (std::size_t index = 0; index < order.size(); ++index)
			order[index] = index;
		std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
			return context.pieces[a].xLow < context.pieces[b].xLow;
		});
		std::map<int, double> levelEnd;
		std::vector<int> preferred(context.pieces.size(), std::numeric_limits<int>::min());
		for (const std::size_t index : order) {
			Piece& piece = context.pieces[index];
			const auto free = [&](int level) {
				const auto found = levelEnd.find(level);
				return found == levelEnd.end() || found->second < piece.xLow - 1e-9;
			};
			int level = preferred[index];
			if (level == std::numeric_limits<int>::min() || !free(level)) {
				level = 0;
				for (int step = 1; !free(level); ++step)
					level = step % 2 == 1 ? (step + 1) / 2 : -(step / 2);
			}
			piece.level = level;
			levelEnd[level] = piece.xHigh();
			for (const std::size_t linkIndex : context.linksAtEnd[index * 2 + (piece.sign > 0 ? 1 : 0)]) {
				const Link& link = context.links[linkIndex];
				if (link.from == index && preferred[link.to] == std::numeric_limits<int>::min())
					preferred[link.to] = level;
			}
		}
		addDiagnostic(context, SceneSeverity::Info, "scene.import.railml.layout",
			"Schematic layout: the preview draws the tracks along their chainage, on levels that keep them apart. "
				+ std::string(located == 0 ? "The file has no OCP coordinates"
										   : std::to_string(located) + " of " + std::to_string(context.ocps.size())
							+ " OCPs have coordinates, which do not place every track (each needs two)"),
			"view");
	}
	// Coordinates that place no station of the layout.
	SceneImportReportRow& geoRow = reportRow(context, "geoCoord");
	geoRow.skippedCount += located - geoRow.convertedCount;
	for (const Piece& piece : context.pieces) {
		SceneTrackView view;
		view.trackId = piece.id;
		view.level = piece.level;
		view.region = piece.region;
		scene.trackViews.push_back(view);
	}
}

// --- Rolling stock ---

struct UnitBuild {
	SceneTrainUnit unit;
	std::vector<std::string> defaulted;
};

void scaledTraction(SceneTrainUnit& unit) {
	const double massKg = unit.physical.mass_of_traction_unit_kg + unit.physical.mass_of_a_wagon_kg * unit.physical.number_of_wagons;
	const double factor = massKg / kReferenceMassKg;
	const double top = unit.physical.max_speed_ms;
	unit.tractionCurve.clear();
	unit.tractionCurve.push_back({0.0, std::min(top, kReferenceBreakMs), kReferenceStartForceN * factor, 0.0, 0.0});
	if (top > kReferenceBreakMs)
		unit.tractionCurve.push_back({kReferenceBreakMs, std::min(top, kReferenceTopMs), kReferenceCurve[0] * factor,
			kReferenceCurve[1] * factor, kReferenceCurve[2] * factor});
	if (top > kReferenceTopMs) {
		const double force = kReferenceCurve[0] + kReferenceCurve[1] * kReferenceTopMs
			+ kReferenceCurve[2] * kReferenceTopMs * kReferenceTopMs;
		unit.tractionCurve.push_back({kReferenceTopMs, top, force * factor, 0.0, 0.0});
	}
}

UnitBuild buildUnit(const Context& context, const std::string& id, const std::vector<std::pair<std::string, int>>& vehicleRefs,
	std::optional<double> lengthM, std::optional<double> speedKmh, std::optional<double> weightT, int& unresolved) {
	UnitBuild build;
	SceneTrainUnit& unit = build.unit;
	unit.id = id;
	unit.hasPhysical = true;
	SceneTrainPhysical& physical = unit.physical;
	std::vector<std::pair<const Vehicle*, int>> vehicles;
	for (const auto& reference : vehicleRefs) {
		const auto found = context.vehicleIndex.find(reference.first);
		if (found == context.vehicleIndex.end()) {
			++unresolved;
			continue;
		}
		vehicles.emplace_back(&context.vehicles[found->second], reference.second);
	}
	bool anyPowered = false;
	for (const auto& entry : vehicles)
		anyPowered = anyPowered || entry.first->powered;
	double tractionMass = 0.0;
	double wagonMass = 0.0;
	int wagons = 0;
	bool massMissing = false;
	double vehicleLength = 0.0;
	bool lengthMissing = vehicles.empty();
	std::optional<double> lowestSpeed;
	int places = 0;
	for (std::size_t index = 0; index < vehicles.size(); ++index) {
		const Vehicle& vehicle = *vehicles[index].first;
		const int count = vehicles[index].second;
		const bool traction = vehicle.powered || (!anyPowered && index == 0);
		if (!vehicle.massT)
			massMissing = true;
		const double mass = vehicle.massT.value_or(0.0) * 1000.0;
		if (traction)
			tractionMass += mass * count;
		else {
			wagonMass += mass * count;
			wagons += count;
		}
		if (vehicle.lengthM)
			vehicleLength += *vehicle.lengthM * count;
		else
			lengthMissing = true;
		if (vehicle.speedKmh)
			lowestSpeed = lowestSpeed ? std::min(*lowestSpeed, *vehicle.speedKmh) : *vehicle.speedKmh;
		places += vehicle.places * count;
	}
	if (!vehicles.empty() && !anyPowered)
		build.defaulted.push_back("traction unit");
	if (vehicles.empty() || massMissing || tractionMass + wagonMass <= 0.0) {
		// The formation weight replaces vehicle masses that are missing.
		if (weightT && *weightT > 0.0) {
			tractionMass = *weightT * 1000.0;
			wagonMass = 0.0;
			wagons = 0;
		} else if (tractionMass + wagonMass <= 0.0) {
			tractionMass = kReferenceMassKg;
			wagonMass = 0.0;
			wagons = 0;
			build.defaulted.push_back("mass");
		} else {
			build.defaulted.push_back("mass of some vehicles");
		}
	}
	physical.mass_of_traction_unit_kg = tractionMass;
	physical.number_of_wagons = wagons;
	physical.mass_of_a_wagon_kg = wagons > 0 ? wagonMass / wagons : 0.0;
	if (lengthM && *lengthM > 0.0)
		physical.length_m = *lengthM;
	else if (!lengthMissing && vehicleLength > 0.0)
		physical.length_m = vehicleLength;
	else {
		physical.length_m = kDefaultLengthM;
		build.defaulted.push_back("length");
	}
	if (speedKmh && *speedKmh > 0.0)
		physical.max_speed_ms = *speedKmh / 3.6;
	else if (lowestSpeed && *lowestSpeed > 0.0)
		physical.max_speed_ms = *lowestSpeed / 3.6;
	else {
		physical.max_speed_ms = kDefaultMaxSpeedKmh / 3.6;
		build.defaulted.push_back("maximum speed");
	}
	physical.max_deceleration_ms2 = kReferenceDecelerationMs2;
	physical.frontal_area_m2 = kReferenceFrontalAreaM2;
	physical.resistance_coefficient = kReferenceResistanceCoefficient;
	physical.jerk_ms3 = kReferenceJerkMs3;
	build.defaulted.push_back("deceleration");
	build.defaulted.push_back("frontal area");
	build.defaulted.push_back("resistance");
	build.defaulted.push_back("jerk");
	scaledTraction(unit);
	build.defaulted.push_back("traction curve");
	if (!vehicles.empty()) {
		unit.hasPassengerCapacity = true;
		unit.passengerCapacity = std::min(places, kMaxPassengerCapacity);
	}
	return build;
}

void buildRollingStock(Context& context, const std::set<std::string>& neededUnits) {
	std::map<std::string, std::vector<std::string>> defaultedUnits; // quantity to unit ids
	std::set<std::string> vehiclesInFormations;
	int unresolved = 0;
	const auto add = [&](UnitBuild build) {
		for (const std::string& quantity : build.defaulted)
			defaultedUnits[quantity].push_back(build.unit.id);
		context.scene.trainUnits.push_back(std::move(build.unit));
	};
	SceneImportReportRow& formationRow = reportRow(context, "formation");
	for (const Formation& formation : context.formations) {
		for (const auto& reference : formation.vehicles)
			vehiclesInFormations.insert(reference.first);
		add(buildUnit(context, formation.id, formation.vehicles, formation.lengthM, formation.speedKmh, formation.weightT,
			unresolved));
		++formationRow.convertedCount;
	}
	formationRow.unresolvedReferences += unresolved;
	if (unresolved > 0)
		addDiagnostic(context, SceneSeverity::Warning, "scene.import.railml.unresolved",
			std::to_string(unresolved) + " vehicle references of formations name a vehicle that the file does not hold",
			"formation");
	SceneImportReportRow& vehicleRow = reportRow(context, "vehicle");
	for (const Vehicle& vehicle : context.vehicles) {
		++vehicleRow.convertedCount;
		if (vehiclesInFormations.count(vehicle.id) > 0)
			continue;
		int none = 0;
		add(buildUnit(context, vehicle.id, {{vehicle.id, 1}}, std::nullopt, std::nullopt, std::nullopt, none));
	}
	if (neededUnits.count(kDefaultUnitId) > 0) {
		int none = 0;
		add(buildUnit(context, kDefaultUnitId, {}, std::nullopt, std::nullopt, std::nullopt, none));
		addDiagnostic(context, SceneSeverity::Info, "scene.import.railml.default",
			"Train parts without a known formation run with unit " + std::string(kDefaultUnitId)
				+ ", which has the values of the reference unit",
			"formation", kDefaultUnitId);
	}
	for (const auto& entry : defaultedUnits) {
		std::string rule;
		if (entry.first == "traction curve")
			rule = "the curve of the reference unit SLT_Sprinter with its forces scaled by unit mass / 151 t, so the unit "
				   "accelerates as the reference does, cut at the maximum speed and constant above 130 km/h";
		else if (entry.first == "deceleration")
			rule = numberText(kReferenceDecelerationMs2) + " m/s2";
		else if (entry.first == "frontal area")
			rule = numberText(kReferenceFrontalAreaM2) + " m2";
		else if (entry.first == "resistance")
			rule = "resistance coefficient " + numberText(kReferenceResistanceCoefficient);
		else if (entry.first == "jerk")
			rule = numberText(kReferenceJerkMs3) + " m/s3";
		else if (entry.first == "length")
			rule = numberText(kDefaultLengthM) + " m";
		else if (entry.first == "maximum speed")
			rule = numberText(kDefaultMaxSpeedKmh) + " km/h";
		else if (entry.first == "mass")
			rule = "151 t, the mass of the reference unit";
		else if (entry.first == "mass of some vehicles")
			rule = "0 t for vehicles without a weight, as the formation has no weight either";
		else if (entry.first == "traction unit")
			rule = "no vehicle has driven axles or an engine, so the first vehicle is the traction unit";
		else
			rule = "see the import notes";
		reportDefault(context, "train unit",
			"Default " + entry.first + " for " + std::to_string(entry.second.size()) + " train units (" + rule
				+ "): " + namedList(entry.second));
	}
}

// --- Timetable ---

std::string choosePeriod(Context& context, bool& failed) {
	failed = false;
	std::map<std::string, int> counts;
	for (const TrainPart& part : context.parts)
		if (!part.period.empty())
			++counts[part.period];
	SceneImportReportRow& row = reportRow(context, "operatingPeriod");
	if (!context.options.operatingPeriod.empty()) {
		if (context.periods.count(context.options.operatingPeriod) == 0 && counts.count(context.options.operatingPeriod) == 0) {
			std::vector<std::string> ids;
			for (const auto& entry : counts)
				ids.push_back(entry.first + " (" + std::to_string(entry.second) + " train parts)");
			addDiagnostic(context, SceneSeverity::Error, "scene.import.railml.operating_period",
				"The file has no operating period " + context.options.operatingPeriod
					+ (ids.empty() ? std::string(". It has none.") : ". It has " + namedList(ids)),
				"operatingPeriod", context.options.operatingPeriod, "Choose one of the operating periods of the file");
			failed = true;
			return "";
		}
		row.convertedCount = 1;
		row.skippedCount = row.sourceCount - row.convertedCount;
		return context.options.operatingPeriod;
	}
	std::string chosen;
	int most = 0;
	for (const auto& entry : counts)
		if (entry.second > most) {
			most = entry.second;
			chosen = entry.first;
		}
	if (!chosen.empty())
		row.convertedCount = 1;
	row.skippedCount = row.sourceCount - row.convertedCount;
	return chosen;
}

// The legs of the pieces that a train part runs over, from its first to its last OCP.
bool buildPath(const Context& context, const TrainPart& part, const std::vector<TrackRun>& tracks, PiecePath& path,
	std::string& reason) {
	path = {};
	if (tracks.empty()) {
		reason = "it has no tracks";
		return false;
	}
	const auto extreme = [&](std::size_t track, const std::string& ocp, int dir, bool atStart) -> std::optional<double> {
		const auto found = context.ocpPositions.find({track, ocp});
		if (found == context.ocpPositions.end())
			return std::nullopt;
		// The start lies before every position of the OCP in the direction of travel, the end after every one.
		return (dir > 0) == atStart ? found->second.front() : found->second.back();
	};
	const auto linkBetween = [&](std::size_t endA, std::size_t endB) {
		for (const std::size_t index : context.linksAtEnd[endA]) {
			const Link& link = context.links[index];
			if ((link.endA == endA && link.endB == endB) || (link.endA == endB && link.endB == endA))
				return index;
		}
		return kNone;
	};
	const Track& firstTrack = context.tracks[tracks.front().track];
	double cursor = extreme(tracks.front().track, part.visits.front().ocp, tracks.front().dir, true)
						.value_or(tracks.front().dir > 0 ? firstTrack.beginPos : firstTrack.endPos);
	std::size_t pending = kNone;
	for (std::size_t index = 0; index < tracks.size(); ++index) {
		const std::size_t trackIndex = tracks[index].track;
		const int dir = tracks[index].dir;
		const Track& track = context.tracks[trackIndex];
		double exitPos = dir > 0 ? track.endPos : track.beginPos;
		double nextCursor = 0.0;
		std::size_t nextJoin = kNone;
		if (index + 1 < tracks.size()) {
			// The piece end ahead that a link joins to the entry of the next track: the one at the OCP where the timetable
			// changes track, else the nearest.
			const std::size_t nextTrack = tracks[index + 1].track;
			const int nextDir = tracks[index + 1].dir;
			const std::string& junction = tracks[index + 1].junction;
			double best = std::numeric_limits<double>::infinity();
			for (const std::size_t pieceIndex : track.pieces) {
				const Piece& piece = context.pieces[pieceIndex];
				const int side = dir > 0 ? 1 : 0;
				const double pos = piece.sidePos(side);
				if ((pos - cursor) * dir < -kSamePositionM)
					continue;
				for (const std::size_t linkIndex : context.linksAtEnd[pieceIndex * 2 + static_cast<std::size_t>(side)]) {
					const Link& link = context.links[linkIndex];
					const std::size_t other = link.endA == pieceIndex * 2 + static_cast<std::size_t>(side) ? link.endB : link.endA;
					const Piece& otherPiece = context.pieces[pieceOf(other)];
					if (otherPiece.track != nextTrack || sideOf(other) != (nextDir > 0 ? 0 : 1)
						|| (otherPiece.track == trackIndex))
						continue;
					const bool atJunction = !junction.empty() && nodeOcpAt(context, other) == junction;
					const double distance = std::fabs(pos - cursor) - (atJunction ? 1e12 : 0.0);
					if (distance < best) {
						best = distance;
						exitPos = pos;
						nextJoin = linkIndex;
						nextCursor = otherPiece.sidePos(sideOf(other));
					}
				}
			}
			if (nextJoin == kNone) {
				reason = "no connection joins track " + track.id + " to track " + context.tracks[nextTrack].id
					+ " in its direction of travel";
				return false;
			}
		} else {
			exitPos = extreme(trackIndex, part.visits.back().ocp, dir, false).value_or(exitPos);
		}
		// Legs over the pieces of this track between cursor and exitPos.
		std::vector<std::size_t> pieces = track.pieces;
		if (dir < 0)
			std::reverse(pieces.begin(), pieces.end());
		const double low = std::min(cursor, exitPos);
		const double high = std::max(cursor, exitPos);
		if ((exitPos - cursor) * dir < -kSamePositionM) {
			reason = "on track " + track.id + " its OCPs lie against its direction of travel";
			return false;
		}
		std::size_t previousPiece = kNone;
		for (const std::size_t pieceIndex : pieces) {
			const Piece& piece = context.pieces[pieceIndex];
			const double from = std::max(low, piece.beginPos);
			const double to = std::min(high, piece.endPos);
			if (to - from <= kSamePositionM)
				continue;
			if (!path.legs.empty()) {
				const std::size_t join = previousPiece != kNone
					? linkBetween(previousPiece * 2 + (dir > 0 ? 1 : 0), pieceIndex * 2 + (dir > 0 ? 0 : 1))
					: pending;
				if (join == kNone || !context.links[join].accepted) {
					reason = "a link it needs on track " + track.id + " is left out";
					return false;
				}
				path.joins.push_back(join);
			}
			path.legs.push_back({pieceIndex, dir, dir > 0 ? from : to, dir > 0 ? to : from});
			previousPiece = pieceIndex;
		}
		pending = nextJoin;
		cursor = nextCursor;
	}
	if (path.legs.empty()) {
		reason = "its first and last OCP are at one place";
		return false;
	}
	const int direction = context.pieces[path.legs.front().piece].sign * path.legs.front().dir;
	for (const Leg& leg : path.legs)
		if (context.pieces[leg.piece].sign * leg.dir != direction) {
			reason = "it changes chainage direction";
			return false;
		}
	return true;
}

// The block indices of a leg in travel order.
std::vector<std::size_t> legBlocks(const Piece& piece, const Leg& leg) {
	std::vector<std::size_t> result;
	const double low = std::min(leg.fromPos, leg.toPos);
	const double high = std::max(leg.fromPos, leg.toPos);
	for (std::size_t index = 0; index < piece.blockIds.size(); ++index)
		if (piece.boundaries[index + 1] > low + kSamePositionM && piece.boundaries[index] < high - kSamePositionM)
			result.push_back(index);
	if (leg.dir < 0)
		std::reverse(result.begin(), result.end());
	return result;
}

// Extends a path by one block before its start (atStart) or after its end, on the same piece or across a link.
bool extendPath(const Context& context, PiecePath& path, bool atStart) {
	Leg& leg = atStart ? path.legs.front() : path.legs.back();
	const Piece& piece = context.pieces[leg.piece];
	const int side = (leg.dir > 0) == atStart ? 0 : 1; // end of the piece in that direction
	const double sidePos = piece.sidePos(side);
	double& moving = atStart ? leg.fromPos : leg.toPos;
	if (std::fabs(moving - sidePos) > kSamePositionM) {
		// The next block boundary on the piece.
		double next = sidePos;
		for (const double boundary : piece.boundaries)
			if ((boundary - moving) * (side == 1 ? 1 : -1) > kSamePositionM
				&& std::fabs(boundary - moving) < std::fabs(next - moving))
				next = boundary;
		moving = next;
		return true;
	}
	const std::size_t end = leg.piece * 2 + static_cast<std::size_t>(side);
	for (const std::size_t linkIndex : context.linksAtEnd[end]) {
		const Link& link = context.links[linkIndex];
		const std::size_t other = link.endA == end ? link.endB : link.endA;
		const Piece& otherPiece = context.pieces[pieceOf(other)];
		Leg added;
		added.piece = pieceOf(other);
		// Before the start the train leaves the other piece at `other`; after the end it enters it there.
		const int otherSide = sideOf(other);
		added.dir = atStart ? (otherSide == 1 ? 1 : -1) : (otherSide == 0 ? 1 : -1);
		const std::size_t blockIndex = otherSide == 0 ? 0 : otherPiece.blockIds.size() - 1;
		const double near = otherPiece.sidePos(otherSide);
		const double far = otherSide == 0 ? otherPiece.boundaries[blockIndex + 1] : otherPiece.boundaries[blockIndex];
		added.fromPos = atStart ? far : near;
		added.toPos = atStart ? near : far;
		if (atStart) {
			path.legs.insert(path.legs.begin(), added);
			path.joins.insert(path.joins.begin(), linkIndex);
		} else {
			path.legs.push_back(added);
			path.joins.push_back(linkIndex);
		}
		return true;
	}
	return false;
}

// The section references of a path: the blocks of each leg, with the blocks at both sides of each link replaced by the
// switch section of the link.
bool pathSections(const Context& context, const PiecePath& path,
	const std::map<std::string, std::vector<const SceneSectionDescriptor*>>& switchSections, std::vector<std::string>& result) {
	result.clear();
	std::vector<std::vector<std::size_t>> blocks;
	for (const Leg& leg : path.legs) {
		blocks.push_back(legBlocks(context.pieces[leg.piece], leg));
		if (blocks.back().empty())
			return false;
	}
	for (std::size_t index = 0; index < path.legs.size(); ++index) {
		const Piece& piece = context.pieces[path.legs[index].piece];
		const std::size_t begin = index > 0 ? 1 : 0;
		const std::size_t count = blocks[index].size();
		const std::size_t end = index + 1 < path.legs.size() ? count - 1 : count;
		for (std::size_t k = begin; k < end; ++k)
			result.push_back(piece.blockIds[blocks[index][k]]);
		if (index + 1 < path.legs.size()) {
			const Link& link = context.links[path.joins[index]];
			const std::string here = piece.blockIds[blocks[index].back()];
			const std::string there = context.pieces[path.legs[index + 1].piece].blockIds[blocks[index + 1].front()];
			const auto found = switchSections.find(link.connectionId);
			const SceneSectionDescriptor* section = nullptr;
			if (found != switchSections.end())
				for (const SceneSectionDescriptor* candidate : found->second)
					if ((candidate->firstBlockId == here && candidate->secondBlockId == there)
						|| (candidate->firstBlockId == there && candidate->secondBlockId == here))
						section = candidate;
			if (section == nullptr)
				return false;
			result.push_back(section->id);
		}
	}
	return !result.empty();
}

bool isStop(const OcpVisit& visit) {
	return visit.type == "stop" || visit.type == "begin" || visit.type == "end";
}

// Warns about the tracks of a macroscopic network that routes run over in both directions. The file has no station
// tracks there, so trains of opposite directions have no place to pass each other.
void reportOpposingTraffic(Context& context) {
	std::unordered_map<std::string, std::size_t> blockPiece;
	for (std::size_t index = 0; index < context.pieces.size(); ++index)
		for (const std::string& block : context.pieces[index].blockIds)
			blockPiece[block] = index;
	std::vector<int> directions(context.pieces.size(), 0); // 1 forward, 2 reversed, 3 both
	for (const SceneRoute& route : context.scene.routes)
		for (const std::string& section : route.blocks) {
			// A plain section is a block id; a switch section is "@blockA@-x1/@blockB@-x2".
			std::vector<std::string> blocks;
			std::istringstream parts(section);
			std::string part;
			while (std::getline(parts, part, '/')) {
				const std::size_t close = part.size() > 1 && part.front() == '@' ? part.find('@', 1) : std::string::npos;
				blocks.push_back(close == std::string::npos ? part : part.substr(1, close - 1));
			}
			for (const std::string& block : blocks) {
				const auto found = blockPiece.find(block);
				if (found != blockPiece.end())
					directions[found->second] |= route.reversed ? 2 : 1;
			}
		}
	std::set<std::string> tracks;
	for (std::size_t index = 0; index < context.pieces.size(); ++index) {
		const Track& track = context.tracks[context.pieces[index].track];
		if (directions[index] == 3 && !track.hasConnection)
			tracks.insert(track.id);
	}
	if (tracks.empty())
		return;
	addDiagnostic(context, SceneSeverity::Warning, "scene.import.railml.opposing_traffic",
		std::to_string(tracks.size())
			+ " tracks of the macroscopic network carry trains in both directions, and the file has no station tracks "
			  "where trains can pass each other. Trains of opposite directions that meet on one of them stop head to head "
			  "and wait until the end of the run: "
			+ namedList(std::vector<std::string>(tracks.begin(), tracks.end())),
		"track", "", "Add passing loops and single-track restrictions in the editor where trains cross");
}

void buildTimetable(Context& context) {
	SceneModel& scene = context.scene;
	SceneImportReportRow& partRow = reportRow(context, "trainPart");
	SceneImportReportRow& trainRow = reportRow(context, "train");
	SceneImportReportRow& stopRow = reportRow(context, "ocpTT");
	const std::string& period = context.period;

	// One service per trainPartSequence of an operational train, with the coupled parts in one composition, and one per
	// remaining train part.
	struct Group {
		std::string id;
		std::string number;
		std::string category;
		std::vector<const TrainPart*> parts;
	};
	std::vector<Group> groups;
	std::set<std::string> grouped;
	std::set<std::string> usedCategories; // categories that an imported service has
	bool anyOperational = false;
	for (const Train& train : context.trains)
		anyOperational = anyOperational || train.type == "operational";
	std::vector<std::string> unknownParts;
	for (const Train& train : context.trains) {
		if (anyOperational && train.type != "operational") {
			++trainRow.skippedCount;
			continue;
		}
		bool used = false;
		for (const TrainSequence& sequence : train.sequences) {
			Group group;
			group.id = train.id + (train.sequences.size() > 1 ? "." + std::to_string(sequence.sequence) : "");
			group.number = train.number;
			group.category = sequence.category;
			for (const auto& reference : sequence.parts) {
				const auto found = context.partIndex.find(reference.second);
				if (found == context.partIndex.end()) {
					++trainRow.unresolvedReferences;
					unknownParts.push_back(reference.second);
					continue;
				}
				grouped.insert(reference.second);
				if (inPeriod(context, context.parts[found->second]))
					group.parts.push_back(&context.parts[found->second]);
			}
			if (!group.parts.empty()) {
				groups.push_back(group);
				used = true;
			}
		}
		if (used)
			++trainRow.convertedCount;
		else
			++trainRow.skippedCount;
	}
	if (anyOperational && trainRow.skippedCount > 0)
		addDiagnostic(context, SceneSeverity::Info, "scene.import.railml.skipped",
			std::to_string(trainRow.skippedCount)
				+ " trains are not converted: commercial trains repeat the operational ones, and operational trains without a "
				  "train part in the operating period have nothing to run",
			"train");
	if (!unknownParts.empty())
		addDiagnostic(context, SceneSeverity::Warning, "scene.import.railml.unresolved",
			std::to_string(unknownParts.size()) + " train part references of trains name a train part that the file does not hold: "
				+ namedList(unknownParts),
			"train");
	for (const TrainPart& part : context.parts)
		if (grouped.count(part.id) == 0 && inPeriod(context, part))
			groups.push_back({part.id, part.number, part.category, {&part}});
	int outOfPeriod = 0;
	for (const TrainPart& part : context.parts)
		if (!inPeriod(context, part))
			++outOfPeriod;
	partRow.skippedCount += outOfPeriod;
	if (!period.empty()) {
		const auto name = context.periods.find(period);
		addDiagnostic(context, SceneSeverity::Info, "scene.import.railml.operating_period",
			"Operating period " + period
				+ (name != context.periods.end() && !name->second.empty() ? " (" + name->second + ")" : std::string())
				+ (context.options.operatingPeriod.empty() ? ", which the most train parts use," : "") + " is imported: "
				+ std::to_string(context.parts.size() - static_cast<std::size_t>(outOfPeriod)) + " train parts; "
				+ std::to_string(outOfPeriod) + " train parts of other operating periods are left out",
			"operatingPeriod", period, "Import again with scene_tool import-railml and --operating-period <id> to choose another period");
	}

	const SceneSectionInventory inventory = buildSceneSectionInventory(scene);
	std::map<std::string, std::vector<const SceneSectionDescriptor*>> switchSections;
	for (const SceneSectionDescriptor& section : inventory.sections)
		if (section.connectionDerived)
			switchSections[section.sourceConnectionId].push_back(&section);

	std::map<std::string, std::vector<std::string>> unresolvedServices; // reason to ids
	std::vector<ServiceDraft> drafts;
	int coupledMismatch = 0;
	int rolledOver = 0;
	int defaultDwell = 0;
	std::set<std::string> neededUnits;
	for (const Group& group : groups) {
		const TrainPart& lead = *group.parts.front();
		for (std::size_t index = 1; index < group.parts.size(); ++index) {
			const auto& visits = group.parts[index]->visits;
			if (visits.size() != lead.visits.size()
				|| !std::equal(visits.begin(), visits.end(), lead.visits.begin(),
					[](const OcpVisit& a, const OcpVisit& b) { return a.ocp == b.ocp; }))
				++coupledMismatch;
		}
		bool unresolvedTracks = false;
		const auto tracks = partTracks(context, lead, unresolvedTracks);
		PiecePath path;
		std::string reason;
		if (unresolvedTracks) {
			unresolvedServices["a track reference names no track, has no direction, or a section has no track"].push_back(group.id);
			partRow.unresolvedReferences += static_cast<int>(group.parts.size());
			continue;
		}
		if (lead.visits.size() < 2 || !buildPath(context, lead, tracks, path, reason)) {
			unresolvedServices[lead.visits.size() < 2 ? std::string("it has fewer than two OCPs") : reason].push_back(group.id);
			partRow.skippedCount += static_cast<int>(group.parts.size());
			continue;
		}
		ServiceDraft draft;
		SceneService& service = draft.service;
		service.id = group.id;
		service.operatingCode = !group.number.empty() ? group.number : lead.number;
		const std::string categoryRef = !group.category.empty() ? group.category : lead.category;
		const auto category = context.categories.find(categoryRef);
		service.category = category != context.categories.end() ? category->second : categoryRef;
		if (category != context.categories.end())
			usedCategories.insert(categoryRef);
		if (lead.speedKmh && *lead.speedKmh > 0.0) {
			service.hasMaximumSpeed = true;
			service.maximumSpeedKmh = *lead.speedKmh;
		}
		std::vector<std::string> units;
		for (const TrainPart* part : group.parts) {
			draft.partIds.push_back(part->id);
			if (context.formationIndex.count(part->formation) > 0)
				units.push_back(part->formation);
			else {
				units.push_back(kDefaultUnitId);
				neededUnits.insert(kDefaultUnitId);
			}
		}
		std::string compositionId;
		for (const std::string& unit : units)
			compositionId += (compositionId.empty() ? "" : "+") + unit;
		service.composition = compositionId;
		if (std::none_of(scene.compositions.begin(), scene.compositions.end(),
				[&](const SceneComposition& composition) { return composition.id == compositionId; }))
			scene.compositions.push_back({compositionId, units});
		// Times, with a day added where they run back by more than twelve hours.
		double previous = -std::numeric_limits<double>::infinity();
		double shift = 0.0;
		bool shifted = false;
		const auto absolute = [&](std::optional<double> value) {
			if (!value)
				return std::numeric_limits<double>::quiet_NaN();
			double time = *value + shift;
			while (time < previous - 43200.0) {
				shift += 86400.0;
				time += 86400.0;
				shifted = true;
			}
			previous = time;
			return time;
		};
		for (std::size_t index = 0; index < lead.visits.size(); ++index) {
			const OcpVisit& visit = lead.visits[index];
			const double arrival = absolute(visit.arrival);
			const double departure = absolute(visit.departure);
			if (index == 0 && !isStop(visit)) {
				draft.hasEntry = !std::isnan(departure) || !std::isnan(arrival);
				draft.entry = !std::isnan(departure) ? departure : arrival;
			}
			if (!isStop(visit))
				continue;
			draft.stopStations.push_back(visit.ocp);
			draft.stopArrival.push_back(index == 0 ? std::numeric_limits<double>::quiet_NaN() : arrival);
			draft.stopDeparture.push_back(index + 1 == lead.visits.size() ? std::numeric_limits<double>::quiet_NaN() : departure);
			draft.stopDwell.push_back(visit.minimalStop.value_or(0.0));
			if (!visit.minimalStop)
				++defaultDwell;
		}
		if (shifted)
			++rolledOver;
		// The route; a route of one section has to run up the chainage, so a shorter one is extended.
		std::vector<std::string> sections;
		const int direction = context.pieces[path.legs.front().piece].sign * path.legs.front().dir;
		if (!pathSections(context, path, switchSections, sections)) {
			unresolvedServices["its route has a link without a switch section"].push_back(group.id);
			partRow.skippedCount += static_cast<int>(group.parts.size());
			continue;
		}
		if (sections.size() == 1 && direction < 0) {
			PiecePath longer = path;
			if ((!draft.hasEntry && extendPath(context, longer, true)) || extendPath(context, longer, false))
				pathSections(context, longer, switchSections, sections);
		}
		if (sections.size() == 1 && direction < 0) {
			unresolvedServices["its route of one block runs down the chainage, which a route of one section cannot"].push_back(group.id);
			partRow.skippedCount += static_cast<int>(group.parts.size());
			continue;
		}
		draft.routeBlocks = sections;
		draft.reversed = direction < 0;
		drafts.push_back(std::move(draft));
		partRow.convertedCount += static_cast<int>(group.parts.size());
	}
	for (const auto& entry : unresolvedServices)
		addDiagnostic(context, SceneSeverity::Warning, "scene.import.railml.service_skipped",
			std::to_string(entry.second.size()) + " trains are not imported because " + entry.first + ": "
				+ namedList(entry.second),
			"trainPart");
	if (coupledMismatch > 0)
		addDiagnostic(context, SceneSeverity::Warning, "scene.import.railml.coupled",
			std::to_string(coupledMismatch)
				+ " coupled train parts call at other OCPs than the first part of their sequence; the service follows the first part",
			"trainPart");
	if (rolledOver > 0)
		addDiagnostic(context, SceneSeverity::Info, "scene.import.railml.midnight",
			std::to_string(rolledOver) + " train parts run past midnight without a day offset; their later times are taken on the next day",
			"trainPart");
	if (defaultDwell > 0) {
		reportDefault(context, "ocpTT",
			std::to_string(defaultDwell) + " stops have no minimal stop time; their minimum dwell is 0 s");
	}

	// Base time and duration from the times.
	double earliest = std::numeric_limits<double>::infinity();
	double latest = -std::numeric_limits<double>::infinity();
	for (const ServiceDraft& draft : drafts) {
		const auto consider = [&](double value) {
			if (std::isnan(value))
				return;
			earliest = std::min(earliest, value);
			latest = std::max(latest, value);
		};
		if (draft.hasEntry)
			consider(draft.entry);
		for (std::size_t index = 0; index < draft.stopStations.size(); ++index) {
			consider(draft.stopArrival[index]);
			consider(draft.stopDeparture[index]);
		}
	}
	double base = 0.0;
	if (std::isfinite(earliest)) {
		base = std::floor(earliest / 60.0) * 60.0;
		scene.baseTime = clockText(static_cast<long long>(base));
		scene.settings.hasDuration = true;
		scene.settings.durationSeconds = std::ceil((latest - base + kDurationMarginSeconds) / 60.0) * 60.0;
	}

	// Services, with their stops bound to the platforms that the route passes.
	std::map<std::string, std::string> routeByBlocks;
	int droppedStops = 0;
	std::vector<std::string> splitServices;
	std::vector<std::string> droppedServices;
	for (ServiceDraft& draft : drafts) {
		std::string key = draft.reversed ? "-" : "+";
		for (const std::string& block : draft.routeBlocks)
			key += block + "\n";
		auto route = routeByBlocks.find(key);
		if (route == routeByBlocks.end()) {
			SceneRoute sceneRoute;
			sceneRoute.id = "route." + draft.service.id;
			sceneRoute.blocks = draft.routeBlocks;
			sceneRoute.reversed = draft.reversed;
			scene.routes.push_back(sceneRoute);
			route = routeByBlocks.emplace(key, sceneRoute.id).first;
		}
		const SceneRoute& sceneRoute = *std::find_if(scene.routes.begin(), scene.routes.end(),
			[&](const SceneRoute& candidate) { return candidate.id == route->second; });
		const SceneRouteTraversal traversal = buildSceneRouteTraversal(scene, sceneRoute, inventory);
		SceneService& service = draft.service;
		service.route = sceneRoute.id;
		if (draft.hasEntry) {
			service.hasEntryTime = true;
			service.entryTimeSeconds = draft.entry - base;
		}
		std::size_t visitIndex = 0;
		std::vector<std::size_t> stopVisits;
		for (std::size_t index = 0; index < draft.stopStations.size(); ++index) {
			std::size_t found = kNone;
			for (std::size_t candidate = visitIndex; candidate < traversal.visits.size(); ++candidate)
				if (traversal.visits[candidate].stationId == draft.stopStations[index]) {
					found = candidate;
					break;
				}
			if (found == kNone) {
				++droppedStops;
				continue;
			}
			SceneStop stop;
			stop.stationId = draft.stopStations[index];
			stop.platformId = traversal.visits[found].platformId;
			if (!std::isnan(draft.stopArrival[index])) {
				stop.hasPlannedArrival = true;
				stop.plannedArrivalSeconds = draft.stopArrival[index] - base;
			}
			if (!std::isnan(draft.stopDeparture[index])) {
				stop.hasPlannedDeparture = true;
				stop.plannedDepartureSeconds = draft.stopDeparture[index] - base;
			}
			stop.dwellSeconds = draft.stopDwell[index];
			service.stops.push_back(stop);
			stopVisits.push_back(traversal.visits[found].sectionIndex);
			++stopRow.convertedCount;
			visitIndex = found + 1;
		}
		if (service.stops.empty() && !service.hasEntryTime) {
			droppedServices.push_back(service.id);
			partRow.convertedCount -= static_cast<int>(draft.partIds.size());
			partRow.skippedCount += static_cast<int>(draft.partIds.size());
			continue;
		}
		const std::size_t limit = static_cast<std::size_t>(RuntimeLimits::kMaxTimetableStops);
		if (service.stops.size() <= limit) {
			scene.services.push_back(service);
			continue;
		}
		// More stops than a service holds: consecutive services that meet at a shared stop.
		splitServices.push_back(service.id);
		std::size_t start = 0;
		int chunkNumber = 0;
		while (start + 1 < service.stops.size()) {
			const std::size_t stop = std::min(start + limit - 1, service.stops.size() - 1);
			SceneService chunk = service;
			chunk.id = service.id + "_" + std::to_string(++chunkNumber);
			chunk.stops.assign(service.stops.begin() + static_cast<std::ptrdiff_t>(start),
				service.stops.begin() + static_cast<std::ptrdiff_t>(stop) + 1);
			if (start > 0) {
				chunk.hasEntryTime = false;
				chunk.stops.front().hasPlannedArrival = false;
			}
			if (stop + 1 < service.stops.size())
				chunk.stops.back().hasPlannedDeparture = false;
			const std::size_t firstSection = start == 0 ? 0 : stopVisits[start];
			const std::size_t lastSection = stop + 1 == service.stops.size() ? sceneRoute.blocks.size() - 1 : stopVisits[stop];
			SceneRoute partRoute;
			partRoute.id = sceneRoute.id + "_" + std::to_string(chunkNumber);
			partRoute.reversed = sceneRoute.reversed;
			partRoute.blocks.assign(sceneRoute.blocks.begin() + static_cast<std::ptrdiff_t>(firstSection),
				sceneRoute.blocks.begin() + static_cast<std::ptrdiff_t>(lastSection) + 1);
			chunk.route = partRoute.id;
			if (std::none_of(scene.routes.begin(), scene.routes.end(), [&](const SceneRoute& r) { return r.id == partRoute.id; }))
				scene.routes.push_back(partRoute);
			scene.services.push_back(chunk);
			start = stop;
		}
	}
	// Passing times, stops that are left out and the train parts of other periods are not converted.
	stopRow.skippedCount = stopRow.sourceCount - stopRow.convertedCount;
	SceneImportReportRow& categoryRow = reportRow(context, "category");
	categoryRow.convertedCount = static_cast<int>(usedCategories.size());
	categoryRow.skippedCount = categoryRow.sourceCount - categoryRow.convertedCount;
	// Routes that no service uses after splitting.
	std::set<std::string> usedRoutes;
	for (const SceneService& service : scene.services)
		usedRoutes.insert(service.route);
	scene.routes.erase(std::remove_if(scene.routes.begin(), scene.routes.end(),
						   [&](const SceneRoute& route) { return usedRoutes.count(route.id) == 0; }),
		scene.routes.end());
	if (droppedStops > 0)
		addDiagnostic(context, SceneSeverity::Warning, "scene.import.railml.stop_skipped",
			std::to_string(droppedStops)
				+ " stops are left out because the route of their train does not pass a platform of the OCP after the previous stop",
			"ocpTT");
	if (!droppedServices.empty())
		addDiagnostic(context, SceneSeverity::Warning, "scene.import.railml.service_skipped",
			std::to_string(droppedServices.size()) + " trains are not imported because none of their stops lies on their route: "
				+ namedList(droppedServices),
			"trainPart");
	if (!splitServices.empty())
		addDiagnostic(context, SceneSeverity::Warning, "scene.import.railml.service_split",
			std::to_string(splitServices.size()) + " trains have more than "
				+ std::to_string(RuntimeLimits::kMaxTimetableStops)
				+ " stops, which a service cannot hold; each runs as consecutive services that meet at a shared stop: "
				+ namedList(splitServices),
			"service");
	reportOpposingTraffic(context);
	if (scene.services.size() > static_cast<std::size_t>(RuntimeLimits::kMaxExpandedTrains))
		addDiagnostic(context, SceneSeverity::Warning, "scene.import.railml.capacity",
			"The scene has " + std::to_string(scene.services.size()) + " services, more than the "
				+ std::to_string(RuntimeLimits::kMaxExpandedTrains) + " trains a run holds; select fewer for a run",
			"service");
	buildRollingStock(context, neededUnits);
}

// The speed profile that the most train sequences of operational trains name.
void choosePreferredSpeedProfile(Context& context) {
	std::map<std::string, int> counts;
	for (const Train& train : context.trains)
		for (const TrainSequence& sequence : train.sequences)
			if (!sequence.speedProfile.empty())
				++counts[sequence.speedProfile];
	int most = 0;
	for (const auto& entry : counts)
		if (entry.second > most) {
			most = entry.second;
			context.preferredSpeedProfile = entry.first;
		}
}

void reportUnsupported(Context& context) {
	for (const auto& entry : context.unsupported) {
		SceneImportReportRow& row = reportRow(context, entry.first.c_str());
		row.sourceCount += entry.second;
		row.skippedCount += entry.second;
		addDiagnostic(context, SceneSeverity::Info, "scene.import.railml.unsupported",
			std::to_string(entry.second) + " " + entry.first + " elements have no counterpart in the scene and are not converted",
			entry.first);
	}
}

bool readDocument(Context& context, pugi::xml_document& document) {
	std::ifstream input(pathFromUtf8(context.sourceFile), std::ios::binary);
	if (!input) {
		addDiagnostic(context, SceneSeverity::Error, "scene.import.railml.read", "The railML file cannot be opened", "file",
			context.sourceFile, "Check the path and the permissions of the file");
		return false;
	}
	const std::string bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
	const pugi::xml_parse_result parsed = document.load_buffer(bytes.data(), bytes.size());
	if (!parsed) {
		addDiagnostic(context, SceneSeverity::Error, "scene.import.railml.format",
			std::string("The file is not railML: it is not well-formed XML (") + parsed.description() + " at byte "
				+ std::to_string(parsed.offset) + ")",
			"file", context.sourceFile, "Choose a railML 2 file");
		return false;
	}
	return true;
}

} // namespace

SceneImportResult importRailmlScene(const std::string& railmlPath, const std::string& sceneDir,
	const std::string& sceneName, const RailmlImportOptions& options) {
	Context context;
	context.sourceFile = railmlPath;
	context.options = options;
	pugi::xml_document document;
	if (!readDocument(context, document))
		return std::move(context.result);
	const xml_node root = document.document_element();
	if (!checkVersion(context, root))
		return std::move(context.result);

	SceneImportReportRow& fileRow = reportRow(context, "file");
	fileRow.sourceCount = 1;
	fileRow.convertedCount = 1;
	for (const xml_node& child : elementChildren(root)) {
		if (named(child, "infrastructure"))
			readInfrastructure(context, child);
		else if (named(child, "rollingstock"))
			readRollingStock(context, child);
		else if (named(child, "timetable"))
			readTimetable(context, child);
		else if (!named(child, "metadata"))
			countUnsupported(context, child);
	}
	if (context.tracks.empty()) {
		addDiagnostic(context, SceneSeverity::Error, "scene.import.railml.empty",
			"The file has no track that can be imported", "file", railmlPath, "Choose a railML file with infrastructure");
		return std::move(context.result);
	}

	context.scene = makeNewSceneModel();
	bool unknownPeriod = false;
	context.period = choosePeriod(context, unknownPeriod);
	if (unknownPeriod)
		return std::move(context.result);
	choosePreferredSpeedProfile(context);
	placeOcps(context);
	buildPieces(context);
	buildBlocks(context);
	buildLinks(context);
	orientAndLayOut(context);
	buildInfrastructure(context);
	buildStations(context);
	buildSignals(context);
	buildViews(context);
	buildTimetable(context);
	if (hasErrors(context.result.diagnostics))
		return std::move(context.result);
	reportUnsupported(context);

	SceneModel& scene = context.scene;
	std::string name = sceneName;
	if (name.empty())
		name = pathFromUtf8(railmlPath).stem().u8string();
	scene.name = name;
	scene.description = "Imported from the railML " + context.version + " file "
		+ pathFromUtf8(railmlPath).filename().u8string()
		+ (context.period.empty() ? std::string() : ", operating period " + context.period);
	// Rows of element kinds that the file does not have are left out.
	for (const SceneImportReportRow& row : context.report)
		if (row.sourceCount != 0 || row.convertedCount != 0 || row.skippedCount != 0 || row.unresolvedReferences != 0)
			scene.importReport.push_back(row);

	SceneSaveResult saved = replaceSceneDirectory(scene, sceneDir);
	for (SceneDiagnostic& diagnostic : saved.diagnostics)
		if (diagnostic.severity == SceneSeverity::Error || diagnostic.code.rfind("scene.save.", 0) == 0)
			context.result.diagnostics.push_back(std::move(diagnostic));
	context.result.wroteScene = saved.success();
	return std::move(context.result);
}
