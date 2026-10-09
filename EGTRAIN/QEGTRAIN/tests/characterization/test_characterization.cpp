// Characterization tests for the simulation core.
//
// A case loads a scene, runs the real DispatchController headless in this
// process and renders what the run did (trajectories, stops, braking points,
// aspect codes, timetable results, delay statistics, blocking times, incident
// boundaries) as text. The text is compared with a golden file in
// tests/characterization/expected. Integers and strings must match exactly,
// floats within an absolute and a relative tolerance.
//
//   test_characterization --fixture DIR --expect DIR --case NAME
//   test_characterization --repeat SCENE[#CASE]...
//
// --repeat runs the steps in one process, in order, and requires that every
// step that appears twice produces exactly the same observations both times.
// SCENE is a scene directory. Without #CASE the scene runs as committed;
// with it, CASE is a name from the case table and sets scenario, services and
// signalling level.
//
// EGTRAIN_UPDATE_EXPECTATIONS=1 rewrites the golden file of --case instead of
// comparing; it is refused when the CI environment variable is set.

#include "app/DispatchController.h"
#include "diagrams/RunResults.h"
#include "simulation/InitialParameters.h"
#include "simulation/Infrastructure.h"
#include "simulation/Optimisation.h"
#include "simulation/RollingStock.h"
#include "simulation/Signalling.h"
#include "simulation/Simulation.h"
#ifdef signals
#undef signals
#endif

#include <QCoreApplication>
#include <QTemporaryDir>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <locale>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

Logger owl;

namespace {

// ---------------------------------------------------------------------------
// Observations: lines of key words and name=value fields.
// ---------------------------------------------------------------------------

constexpr double kFloatAbsTolerance = 1e-4;
constexpr double kFloatRelTolerance = 1e-9;
// Values read back from the 6-significant-digit statistics file.
constexpr double kStatAbsTolerance = 1e-9;
constexpr double kStatRelTolerance = 1e-5;

struct Field {
	enum class Kind { Int,
		Float,
		Stat,
		Text };
	std::string name; // Empty for a bare token.
	Kind kind = Kind::Text;
	long long integer = 0;
	double real = 0.0;
	std::string text;
};

struct Line {
	std::string key; // Space-separated words before the first field.
	std::vector<Field> fields;
};

using Observation = std::vector<Line>;

std::string formatReal(double value) {
	if (std::fabs(value) < 5e-7)
		value = 0.0; // No "-0.000000".
	std::ostringstream out;
	out.imbue(std::locale::classic());
	out.setf(std::ios::fixed);
	out.precision(6);
	out << value;
	return out.str();
}

std::string cleanText(const std::string& value) {
	if (value.empty())
		return "-";
	std::string out = value;
	std::replace(out.begin(), out.end(), ' ', '_');
	return out;
}

Field integerField(const std::string& name, long long value) {
	Field field;
	field.name = name;
	field.kind = Field::Kind::Int;
	field.integer = value;
	field.text = std::to_string(value);
	return field;
}

Field realField(const std::string& name, double value) {
	Field field;
	field.name = name;
	field.kind = Field::Kind::Float;
	field.real = value;
	field.text = formatReal(value);
	return field;
}

Field statField(const std::string& name, double value) {
	Field field = realField(name, value);
	field.kind = Field::Kind::Stat;
	return field;
}

Field textField(const std::string& name, const std::string& value) {
	Field field;
	field.name = name;
	field.kind = Field::Kind::Text;
	field.text = cleanText(value);
	return field;
}

Field resultField(const std::string& name, const RunResultValue& value) {
	return value.available ? realField(name, value.value) : textField(name, "n/a");
}

std::string renderLine(const Line& line) {
	std::string out = line.key;
	for (const Field& field : line.fields) {
		out += ' ';
		if (!field.name.empty())
			out += field.name + "=";
		out += field.text;
	}
	return out;
}

bool sameField(const Field& a, const Field& b) {
	if (a.name != b.name || a.kind != b.kind || a.text != b.text)
		return false;
	if (a.kind == Field::Kind::Int)
		return a.integer == b.integer;
	if (a.kind == Field::Kind::Float || a.kind == Field::Kind::Stat)
		return a.real == b.real || (std::isnan(a.real) && std::isnan(b.real));
	return true;
}

bool sameLine(const Line& a, const Line& b) {
	if (a.key != b.key || a.fields.size() != b.fields.size())
		return false;
	for (size_t i = 0; i < a.fields.size(); ++i)
		if (!sameField(a.fields[i], b.fields[i]))
			return false;
	return true;
}

// ---------------------------------------------------------------------------
// Case table
// ---------------------------------------------------------------------------

constexpr int kNoSignallingArea = -1;
// The fixture is one track of this length. A level is applied by one
// network-wide signalling area.
constexpr double kFixtureLengthKm = 16.0;

struct CaseSpec {
	std::string name;
	std::string scenario;
	std::vector<std::string> services;
	int level = kNoSignallingArea;
	// Empty, or "#<issue> <reason>" for behaviour that is pinned but wrong.
	// The golden file carries the same text in its header.
	std::string knownWrong;
	// Declares the single-track restriction blocks 1-B0 to 4-B0, protected by 0-B0 and 5-B0.
	bool singleTrack = false;
};

// Cases whose current behaviour is wrong, with the open issue that describes it.
const std::map<std::string, std::string> kKnownWrong = {};

std::string knownWrongMarker(const std::string& name) {
	const auto found = kKnownWrong.find(name);
	return found == kKnownWrong.end() ? "" : found->second;
}

std::vector<CaseSpec> buildCaseTable() {
	std::vector<CaseSpec> cases;
	cases.push_back({"single-train", "baseline", {"T1"}, kNoSignallingArea, ""});
	const struct {
		const char* prefix;
		const char* scenario;
		std::vector<std::string> services;
		int lastLevel; // The last level run besides "none"; -1 runs "none" only.
	} groups[] = {
		{"follow", "baseline", {"F1", "F2"}, 5},
		{"sf-forward", "signal-failure-forward", {"F1", "F2"}, 5},
		{"sf-reverse", "signal-failure-reverse", {"R1", "R2"}, 5},
		{"sf-adjacent", "signal-failure-adjacent", {"F1", "F2"}, 2},
		{"sf-staggered", "signal-failure-staggered", {"F1", "F2"}, 2},
		{"sf-last", "signal-failure-last", {"F1", "F2"}, 2},
		{"sf-first", "signal-failure-first", {"F1", "F2"}, 2},
		{"sf-entered", "signal-failure-entered", {"F1", "F2"}, -1},
		{"same-entry", "baseline", {"T1", "F1"}, 5},
	};
	for (const auto& group : groups) {
		const std::string none = std::string(group.prefix) + "-level-none";
		cases.push_back({none, group.scenario, group.services, kNoSignallingArea, knownWrongMarker(none)});
		for (int level = 0; level <= group.lastLevel; ++level) {
			const std::string name = std::string(group.prefix) + "-level-" + std::to_string(level);
			cases.push_back({name, group.scenario, group.services, level, knownWrongMarker(name)});
		}
	}
	// F2 is listed before L1 in the scene but L1 is due first (60 s against 120 s); both wait while the first section is
	// blocked, so L1 has to enter first.
	for (int level = 0; level <= 2; ++level)
		cases.push_back({"entry-order-level-" + std::to_string(level), "signal-failure-first", {"F2", "L1"}, level, ""});
	// L1 stays 100 s at C, so F2 is held behind it at C.
	for (int level = 3; level <= 4; ++level)
		cases.push_back({"late-leader-level-" + std::to_string(level), "baseline", {"L1", "F2"}, level, ""});
	// S1 runs from A to B and R1 from C to A over the restricted section: R1 has to wait in front of it.
	const std::string singleTrackNone = "single-track-level-none";
	cases.push_back({singleTrackNone, "baseline", {"S1", "R1"}, kNoSignallingArea, knownWrongMarker(singleTrackNone), true});
	for (int level = 0; level <= 5; ++level) {
		const std::string name = "single-track-level-" + std::to_string(level);
		cases.push_back({name, "baseline", {"S1", "R1"}, level, knownWrongMarker(name), true});
	}
	// F1 holds the restricted section and F2 follows it in the same direction: the restriction does not delay F2.
	for (int level = 3; level <= 4; ++level)
		cases.push_back({"single-track-follow-level-" + std::to_string(level), "baseline", {"F1", "F2"}, level, "", true});
	return cases;
}

const CaseSpec* findCase(const std::vector<CaseSpec>& cases, const std::string& name) {
	for (const CaseSpec& spec : cases)
		if (spec.name == name)
			return &spec;
	return nullptr;
}

// ---------------------------------------------------------------------------
// Running one case
// ---------------------------------------------------------------------------

class CoutSilencer {
public:
	CoutSilencer() : saved_(std::cout.rdbuf(sink_.rdbuf())) {}
	~CoutSilencer() { std::cout.rdbuf(saved_); }

private:
	std::ostringstream sink_;
	std::streambuf* saved_;
};

// Checks the signal states of every snapshot against the section codes of all
// routes, which is what the canvas is given: one entry per section and
// direction, the most restrictive code of the route copies, the level of the
// section, and the failure flag exactly inside the incident window. The blocked
// flag of the sections has to follow the same window.
class SnapshotSignalChecker {
public:
	void check(const GuiSimulationSnapshot& snapshot) {
		struct Expected {
			int code = 0;
			int level = kGuiSignalNoLevel;
		};
		std::map<std::pair<std::string, bool>, Expected> expected;
		const int routeCount = std::min(N_Routes, static_cast<int>(train_route.size()));
		for (int r = 0; r < routeCount; ++r) {
			const Route& route = train_route[r];
			for (int b = 0; b < route.N_Block_Sections; ++b) {
				const Section& section = route.sequence_of_block_sections[b];
				const int code = static_cast<int>(section.code);
				const int level = guiSignalHasLevel(section.SignallingLevel) ? section.SignallingLevel : kGuiSignalNoLevel;
				for (const std::string& id : signalIds(section.ID)) {
					const auto inserted = expected.try_emplace({id, route.reversed_direction}, Expected{code, level});
					if (inserted.second || level == kGuiSignalNoLevel)
						continue;
					Expected& entry = inserted.first->second;
					if (entry.level == kGuiSignalNoLevel) {
						entry = {code, level};
						continue;
					}
					if (entry.level != level)
						fail(snapshot.timestep,
							"route copies of " + describe({id, route.reversed_direction}) + " have levels "
								+ std::to_string(entry.level) + " and " + std::to_string(level));
					if (guiSignalRestriction(code) < guiSignalRestriction(entry.code))
						entry.code = code;
				}
			}
		}
		std::set<std::string> failedIds;
		for (const SimulationIncident& incident : simulationIncidents) {
			const bool hasEnd = incident.hasEndSeconds || incident.endSeconds != 0.0;
			if (incident.type == "signal_failure" && snapshot.timestep >= incident.startSeconds
				&& (!hasEnd || snapshot.timestep <= incident.endSeconds))
				failedIds.insert(incident.resolvedSectionIDs.begin(), incident.resolvedSectionIDs.end());
		}
		failedIds.erase("");
		// The sections of an active failure are drawn as blocked, and no other section is.
		std::set<std::string> blockedIds;
		for (const GuiSectionState& section : snapshot.sectionStates)
			if (section.blocked)
				blockedIds.insert(section.sectionId);
		if (blockedIds != failedIds)
			fail(snapshot.timestep,
				"the blocked sections are [" + joined(blockedIds) + "], the active signal failures have [" + joined(failedIds) + "]");
		std::set<std::pair<std::string, bool>> seen;
		for (const GuiSignalState& state : snapshot.signalStates) {
			const auto key = std::make_pair(state.sectionId, state.reversedDirection);
			if (!seen.insert(key).second) {
				fail(snapshot.timestep, "two entries for " + describe(key));
				continue;
			}
			const auto found = expected.find(key);
			if (found == expected.end()) {
				fail(snapshot.timestep, "an entry for " + describe(key) + " that no route has");
				continue;
			}
			if (state.code != found->second.code || state.level != found->second.level)
				fail(snapshot.timestep,
					describe(key) + " has code " + std::to_string(state.code) + " level " + std::to_string(state.level)
						+ ", the route copies give code " + std::to_string(found->second.code) + " level "
						+ std::to_string(found->second.level));
			if (state.failed != (failedIds.count(state.sectionId) > 0))
				fail(snapshot.timestep, describe(key) + " has failed=" + (state.failed ? "1" : "0"));
		}
		for (const auto& entry : expected)
			if (!seen.count(entry.first))
				fail(snapshot.timestep, "no entry for " + describe(entry.first));
	}

	const std::vector<std::string>& problems() const { return problems_; }

private:
	// The block IDs a section signals for. A section made at a switch has the
	// form "@a-B0@-1.5/@b-B1@-2.5" and signals for both blocks.
	static std::vector<std::string> signalIds(const std::string& id) {
		if (id.empty())
			return {};
		if (id.find('/') == std::string::npos)
			return {id};
		const std::size_t first = id.find("@-");
		const std::size_t middle = id.find("/@", first == std::string::npos ? 0 : first + 1);
		const std::size_t last = id.find("@-", middle == std::string::npos ? 0 : middle + 1);
		if (first == std::string::npos || middle == std::string::npos || last == std::string::npos)
			return {};
		return {id.substr(0, first + 1), id.substr(middle + 1, last - middle)};
	}

	static std::string describe(const std::pair<std::string, bool>& key) {
		return key.first + (key.second ? " (reversed)" : " (forward)");
	}

	static std::string joined(const std::set<std::string>& ids) {
		std::string text;
		for (const std::string& id : ids)
			text += (text.empty() ? "" : ",") + id;
		return text;
	}

	void fail(int step, const std::string& text) {
		if (problems_.size() < 10)
			problems_.push_back("step " + std::to_string(step) + ": " + text);
	}

	std::vector<std::string> problems_;
};

// Reads the signalling state after every simulated second through the same
// signal the GUI uses.
class StepRecorder : public QObject {
public:
	struct Authority {
		std::string part, section;
		double position = 0.0;
		bool reversed = false;
	};

	struct Boundary {
		int step = 0;
		std::vector<Authority> authorities; // The SignalFailure movement authorities.
		std::vector<std::string> blocked;
	};

	struct RouteCodes {
		int routeIndex = 0;
		std::vector<std::string> sectionIds;
		// Per section: (step, code) at every change, starting with step 0.
		std::vector<std::vector<std::pair<int, double>>> changes;
	};

	StepRecorder(const std::vector<int>& routes, const std::set<int>& boundarySteps)
		: boundarySteps_(boundarySteps) {
		for (int routeIndex : routes) {
			RouteCodes route;
			route.routeIndex = routeIndex;
			const Route& source = train_route[routeIndex];
			route.sectionIds.reserve(source.N_Block_Sections);
			for (int b = 0; b < source.N_Block_Sections; ++b)
				route.sectionIds.push_back(source.sequence_of_block_sections[b].ID);
			route.changes.resize(source.N_Block_Sections);
			routes_.push_back(std::move(route));
		}
	}

	void onSnapshotAvailable() {
		const auto snapshot = simulation.takeSimulationSnapshot();
		if (!snapshot)
			return;
		const int step = snapshot->timestep;
		++callbacks_;
		signalChecker_.check(*snapshot);
		for (RouteCodes& route : routes_) {
			const Route& source = train_route[route.routeIndex];
			for (int b = 0; b < source.N_Block_Sections; ++b) {
				auto& changes = route.changes[b];
				const double code = source.sequence_of_block_sections[b].code;
				if (changes.empty() || changes.back().second != code)
					changes.emplace_back(step, code);
			}
		}
		if (boundarySteps_.count(step)) {
			Boundary boundary;
			boundary.step = step;
			for (const MovementAuthority& authority : ETCS_MA) {
				if (authority.type != "SignalFailure")
					continue;
				boundary.authorities.push_back(
					{authority.typePart, authority.BSID, authority.AbsPosEoA, authority.ReversedDirection});
			}
			for (const GuiSectionState& section : snapshot->sectionStates)
				if (section.blocked)
					boundary.blocked.push_back(section.sectionId);
			boundaries_.push_back(std::move(boundary));
		}
	}

	int callbacks() const { return callbacks_; }
	const SnapshotSignalChecker& signalChecker() const { return signalChecker_; }
	const std::vector<RouteCodes>& routes() const { return routes_; }
	const std::vector<Boundary>& boundaries() const { return boundaries_; }

private:
	std::set<int> boundarySteps_;
	std::vector<RouteCodes> routes_;
	std::vector<Boundary> boundaries_;
	int callbacks_ = 0;
	SnapshotSignalChecker signalChecker_;
};

struct TrainTrack {
	std::string name;
	int routeIndex = 0;
	double length = 0.0;
	double entry = 0.0;
	int first = 0; // First and last active step; first > last when never active.
	int last = -1;
	const std::vector<double>* position = nullptr;
	const std::vector<double>* speed = nullptr;
};

struct Stop {
	int first = 0;
	int last = 0;
	double position = 0.0;
};

struct Separation {
	std::string leader, follower;
	bool overlap = false; // False when the two trains are never in the network together.
	int step = 0;
	double gap = 0.0; // Rear of the leader minus head of the follower, at its smallest.
};

struct RunOutcome {
	Observation observation;
	std::vector<std::string> invariantFailures;
	std::string error;
	int steps = 0;
};

std::vector<std::string> splitWords(const std::string& text) {
	std::istringstream in(text);
	std::vector<std::string> words;
	std::string word;
	while (in >> word)
		words.push_back(word);
	return words;
}

bool parseReal(const std::string& text, double& value) {
	std::istringstream in(text);
	in.imbue(std::locale::classic());
	in >> value;
	return !in.fail() && in.eof();
}

// Rows of a station statistics file (Stats_Stations.txt, or Pos&Neg_Stats_Stations.txt
// with the early arrivals as negative delays) whose name starts with a letter, keyed
// by the header names. The files have six significant digits.
bool readStationStats(const std::string& path, const std::string& keyPrefix, Observation& lines) {
	std::ifstream in(path);
	if (!in)
		return false;
	std::string text;
	std::vector<std::string> header;
	while (std::getline(in, text)) {
		const std::vector<std::string> words = splitWords(text);
		if (words.empty())
			continue;
		if (header.empty()) {
			header = words;
			continue;
		}
		if (!std::isalpha(static_cast<unsigned char>(words[0][0])))
			continue;
		Line line;
		line.key = keyPrefix + " " + words[0];
		for (size_t i = 1; i < words.size(); ++i) {
			const std::string name = i < header.size() ? header[i] : "col" + std::to_string(i);
			double value = 0.0;
			if (parseReal(words[i], value))
				line.fields.push_back(statField(name, value));
			else
				line.fields.push_back(textField(name, words[i]));
		}
		lines.push_back(std::move(line));
	}
	return true;
}

std::vector<Stop> findStops(const TrainTrack& train) {
	constexpr double kStandstill = 1e-3;
	constexpr int kMinimumStopSteps = 5;
	std::vector<Stop> stops;
	const auto& speed = *train.speed;
	const auto& position = *train.position;
	int t = train.first;
	while (t <= train.last) {
		if (speed[t] >= kStandstill) {
			++t;
			continue;
		}
		int end = t;
		while (end + 1 <= train.last && speed[end + 1] < kStandstill)
			++end;
		if (end - t + 1 >= kMinimumStopSteps)
			stops.push_back({t, end, position[t]});
		t = end + 1;
	}
	return stops;
}

std::string joinSet(const std::vector<std::string>& values) {
	if (values.empty())
		return "-";
	std::string out;
	for (const std::string& value : values) {
		if (!out.empty())
			out += ',';
		out += value;
	}
	return out;
}

std::vector<TrainTrack> collectTracks() {
	std::vector<TrainTrack> tracks;
	for (int i = 0; i < numRegions; ++i) {
		const Train& train = regional_train[i];
		TrainTrack track;
		track.name = train.trainDescription;
		track.routeIndex = train.indexOfRoute;
		track.length = train.train_length;
		track.entry = train.departure_time;
		const int available = static_cast<int>(std::min(train.instant_spatial_position.size(),
			train.instant_train_speed.size()));
		track.first = std::max(static_cast<int>(train.departure_time), train.earliestActiveTrajectoryIndex);
		track.last = std::min(train.End_Time, available - 1);
		track.position = &train.instant_spatial_position;
		track.speed = &train.instant_train_speed;
		tracks.push_back(track);
	}
	return tracks;
}

// Each train with the train that entered before it on the same route.
std::vector<Separation> measureSeparations(const std::vector<TrainTrack>& tracks) {
	std::vector<size_t> byEntry(tracks.size());
	for (size_t i = 0; i < tracks.size(); ++i)
		byEntry[i] = i;
	std::stable_sort(byEntry.begin(), byEntry.end(), [&tracks](size_t a, size_t b) {
		return tracks[a].entry < tracks[b].entry;
	});
	std::vector<Separation> separations;
	for (size_t i = 0; i < byEntry.size(); ++i) {
		const TrainTrack& follower = tracks[byEntry[i]];
		const TrainTrack* leader = nullptr;
		for (size_t j = i; j-- > 0;) {
			if (tracks[byEntry[j]].routeIndex == follower.routeIndex) {
				leader = &tracks[byEntry[j]];
				break;
			}
		}
		if (!leader)
			continue;
		Separation separation;
		separation.leader = leader->name;
		separation.follower = follower.name;
		separation.gap = std::numeric_limits<double>::infinity();
		for (int t = std::max(leader->first, follower.first); t <= std::min(leader->last, follower.last); ++t) {
			const double gap = ((*leader->position)[t] - leader->length) - (*follower.position)[t];
			if (gap < separation.gap) {
				separation.gap = gap;
				separation.step = t;
				separation.overlap = true;
			}
		}
		separations.push_back(separation);
	}
	return separations;
}

// A run reports one arrival delay per train and station, whichever output is read.
// The total of a station in Stats_Stations.txt is the sum of the late arrivals the
// timetable results report there, the total in Pos&Neg_Stats_Stations.txt the sum of
// all of them, and N_StopTrains counts the arrival delays. A train that did not arrive,
// or has no planned arrival, has no delay in the results and is not counted.
std::vector<std::string> findStatisticsViolations(const Observation& obs, const std::vector<TimetableResultRow>& rows) {
	std::vector<std::string> failures;
	int comparedLate = 0;
	int comparedSigned = 0;
	for (const Line& line : obs) {
		const bool late = line.key.rfind("stats ", 0) == 0;
		if (!late && line.key.rfind("signed_stats ", 0) != 0)
			continue;
		const std::string station = line.key.substr(line.key.find(' ') + 1);
		if (station.rfind("Ent_", 0) == 0 || station == "DwT_Dist" || station == "TOTALS" || station == "Final_Station")
			continue;
		double expectedTotal = 0.0;
		int expectedCount = 0;
		for (const TimetableResultRow& row : rows) {
			if (row.stationId != station || !row.arrivalDelaySeconds.available)
				continue;
			expectedTotal += late ? std::max(0.0, row.arrivalDelaySeconds.value) : row.arrivalDelaySeconds.value;
			++expectedCount;
		}
		const std::string file = late ? "Stats_Stations.txt" : "Pos&Neg_Stats_Stations.txt";
		for (const Field& field : line.fields) {
			if (field.name == "Total_Delay" || field.name == "N_StopTrains")
				++(late ? comparedLate : comparedSigned);
			if (field.name == "Total_Delay" && std::fabs(field.real - expectedTotal) > std::max(0.5, 1e-5 * std::fabs(expectedTotal)))
				failures.push_back(file + " reports a total delay of " + formatReal(field.real) + " s at " + station
					+ ", the timetable results give " + formatReal(expectedTotal) + " s");
			if (field.name == "N_StopTrains" && std::fabs(field.real - expectedCount) > 0.5)
				failures.push_back(file + " counts " + formatReal(field.real) + " trains at " + station
					+ ", the timetable results give an arrival delay for " + std::to_string(expectedCount));
		}
	}
	const bool anyDelay = std::any_of(rows.begin(), rows.end(), [](const TimetableResultRow& row) { return row.arrivalDelaySeconds.available; });
	if (anyDelay && comparedLate == 0)
		failures.push_back("Stats_Stations.txt holds no station row to compare with the timetable results");
	if (anyDelay && comparedSigned == 0)
		failures.push_back("Pos&Neg_Stats_Stations.txt holds no station row to compare with the timetable results");
	return failures;
}

// Checks that hold for the fixture whatever the golden file says.
std::vector<std::string> findInvariantViolations(const CaseSpec& spec, const std::vector<TrainTrack>& tracks,
	const std::vector<Separation>& separations, const std::vector<TimetableResultRow>& rows) {
	// The limits are the fixture's rolling stock: top speed 36.111111111111 m/s,
	// starting force 209000 N on 151000 kg, braking limit 0.75 m/s2 plus
	// running resistance. A case with a known-wrong marker is exempt from
	// the checks on stops, separation and the timetable. A case without a
	// signalling area is exempt from the check for overlapping trains: a
	// scene without a signalling level does not separate trains.
	constexpr double kTopSpeed = 36.111111111111 + 1e-6;
	constexpr double kMaxAcceleration = 209000.0 / 151000.0 + 1e-3;
	constexpr double kMaxDeceleration = 0.95;
	constexpr double kBlockLength = 2000.0;
	constexpr double kStopTolerance = 0.01;	   // Stops at a platform or block boundary are this exact.
	constexpr double kPositionSettling = 0.01; // A train settling at a stop moves back by millimetres.
	constexpr double kStationBegin = 8000.0;   // Station B, midway on the route axis.
	constexpr double kFollowMargin = 200.0;	   // A stop behind another train is within this of its rear.
	std::vector<std::string> failures;
	auto fail = [&failures](const std::string& message) { failures.push_back(message); };
	const bool waived = !spec.knownWrong.empty();
	for (const TrainTrack& track : tracks) {
		if (track.first > track.last)
			continue;
		const auto& x = *track.position;
		const auto& v = *track.speed;
		double reachedStation = -1.0;
		for (int t = track.first; t <= track.last; ++t) {
			const std::string at = track.name + " at t=" + std::to_string(t);
			if (v[t] > kTopSpeed)
				fail(at + ": speed " + formatReal(v[t]) + " above the top speed");
			if (v[t] < -1e-9)
				fail(at + ": negative speed");
			if (t > track.first) {
				if (x[t] < x[t - 1] - kPositionSettling)
					fail(at + ": position goes backwards by " + formatReal(x[t - 1] - x[t]) + " m");
				if (v[t] - v[t - 1] > kMaxAcceleration)
					fail(at + ": acceleration " + formatReal(v[t] - v[t - 1]) + " m/s2 above the starting effort");
				if (v[t - 1] - v[t] > kMaxDeceleration)
					fail(at + ": deceleration " + formatReal(v[t - 1] - v[t]) + " m/s2 above the braking limit");
			}
			if (reachedStation < 0.0 && x[t] >= kStationBegin - 1.0)
				reachedStation = t;
		}
		if (reachedStation >= 0.0 && reachedStation - track.first < (kStationBegin - 1.0) / kTopSpeed)
			fail(track.name + ": reaches station B faster than the top speed allows");
		if (waived)
			continue;
		for (const Stop& stop : findStops(track)) {
			const double offset = std::fmod(stop.position, kBlockLength);
			const bool onBoundary = offset < kStopTolerance || kBlockLength - offset < kStopTolerance;
			bool behindTrain = false;
			for (const TrainTrack& other : tracks) {
				if (&other == &track || other.routeIndex != track.routeIndex || stop.first < other.first
					|| stop.first > other.last)
					continue;
				const double rear = (*other.position)[stop.first] - other.length;
				behindTrain = behindTrain || (rear >= stop.position && rear - stop.position <= kFollowMargin);
			}
			if (!onBoundary && !behindTrain)
				fail(track.name + ": stops mid-block at x=" + formatReal(stop.position) + " from t="
					+ std::to_string(stop.first));
		}
	}
	if (!waived && spec.level != kNoSignallingArea)
		for (const Separation& separation : separations)
			if (separation.overlap && separation.gap < 0.0)
				fail(separation.follower + " overlaps " + separation.leader + " by "
					+ formatReal(-separation.gap) + " m at t=" + std::to_string(separation.step));
	if (spec.singleTrack && !waived && spec.level != kNoSignallingArea) {
		// S1 runs on routeAB and R1 on route1, so the check above does not pair them. The restricted section with its
		// protected sections is 0-B0 to 5-B0: the whole of routeAB (0 to 8000 m) for S1 and 4000 to 16000 m for R1.
		const TrainTrack* forward = nullptr;
		const TrainTrack* reversed = nullptr;
		for (const TrainTrack& track : tracks) {
			if (track.name == "S1-1")
				forward = &track;
			if (track.name == "R1-1")
				reversed = &track;
		}
		if (forward && reversed)
			for (int t = std::max(forward->first, reversed->first); t <= std::min(forward->last, reversed->last); ++t) {
				const double s = (*forward->position)[t];
				const double r = (*reversed->position)[t];
				if (s >= 0.0 && s - forward->length < 6 * kBlockLength
					&& r >= 2 * kBlockLength && r - reversed->length < 8 * kBlockLength) {
					fail("S1 and R1 are inside the single-track section together at t=" + std::to_string(t));
					break;
				}
			}
	}
	for (const TimetableResultRow& row : rows) {
		if (!waived && row.plannedArrivalSeconds.available && row.plannedDepartureSeconds.available
			&& row.simulatedArrivalSeconds.available && row.simulatedDepartureSeconds.available) {
			const double planned = row.plannedDepartureSeconds.value - row.plannedArrivalSeconds.value;
			const double simulated = row.simulatedDepartureSeconds.value - row.simulatedArrivalSeconds.value;
			if (simulated < planned - 1e-9)
				fail(row.trainId + " at " + row.stationId + ": dwell " + formatReal(simulated)
					+ " s is shorter than planned " + formatReal(planned) + " s");
			if (row.simulatedDepartureSeconds.value < row.plannedDepartureSeconds.value - 1e-9)
				fail(row.trainId + " at " + row.stationId + ": leaves before the planned departure");
		}
	}

	return failures;
}

// Driver for one run. Everything read from the simulation globals happens
// here, so the next run starts from whatever prepareScene resets.
RunOutcome runCase(const std::string& sceneDir, const CaseSpec& spec, bool checkInvariants) {
	RunOutcome outcome;
	SceneLoadResult loaded = loadScene(sceneDir);
	if (hasErrors(loaded.diagnostics)) {
		for (const SceneDiagnostic& diagnostic : loaded.diagnostics)
			outcome.error += toDisplayText(diagnostic) + "\n";
		return outcome;
	}
	if (spec.level != kNoSignallingArea) {
		SceneSignallingArea area;
		area.id = "area.all";
		area.startKm = 0.0;
		area.endKm = kFixtureLengthKm;
		area.level = spec.level;
		loaded.scene.signallingAreas = {area};
	}
	if (spec.singleTrack)
		loaded.scene.singleTrackRestrictions = {{"1-B0", "4-B0", "0-B0", "5-B0"}};
	SceneRunSelection selection;
	for (const std::string& service : spec.services)
		selection.insert({service, 1});

	QTemporaryDir outputDir;
	if (!outputDir.isValid()) {
		outcome.error = "cannot create a temporary output directory\n";
		return outcome;
	}
	initial_variables.GUI = 0;
	initial_variables.TSM = 0;
	initial_variables.RChoice = 0;
	initial_variables.OutputMainFolder = outputDir.path().toStdString();
	InputMainFolder.clear();
	initial_variables.InputMainFolder.clear();

	CoutSilencer silencer;
	const std::vector<SceneDiagnostic> diagnostics =
		simulation.prepareScene(loaded.scene, spec.scenario, selection);
	if (hasErrors(diagnostics)) {
		for (const SceneDiagnostic& diagnostic : diagnostics)
			outcome.error += toDisplayText(diagnostic) + "\n";
		return outcome;
	}
	if (timestep != 1.0) {
		outcome.error = "the harness indexes trajectories by whole seconds; timestep is " + formatReal(timestep) + "\n";
		return outcome;
	}

	std::vector<int> routesInUse;
	for (int i = 0; i < numRegions; ++i) {
		const int routeIndex = regional_train[i].indexOfRoute;
		if (routeIndex >= 0 && routeIndex < N_Routes
			&& std::find(routesInUse.begin(), routesInUse.end(), routeIndex) == routesInUse.end())
			routesInUse.push_back(routeIndex);
	}
	std::sort(routesInUse.begin(), routesInUse.end());

	std::set<int> boundarySteps;
	for (const SimulationIncident& incident : simulationIncidents) {
		boundarySteps.insert(static_cast<int>(incident.startSeconds) - 1);
		boundarySteps.insert(static_cast<int>(incident.startSeconds));
		if (incident.hasEndSeconds) {
			boundarySteps.insert(static_cast<int>(incident.endSeconds));
			boundarySteps.insert(static_cast<int>(incident.endSeconds) + 1);
		}
	}

	StepRecorder recorder(routesInUse, boundarySteps);
	QObject::connect(&simulation, &DispatchController::snapshotAvailable, &recorder, [&recorder]() { recorder.onSnapshotAvailable(); }, Qt::DirectConnection);
	const int horizon = static_cast<int>(initial_variables.times);
	simulation.runSimulation();
	QObject::disconnect(&simulation, &DispatchController::snapshotAvailable, &recorder, nullptr);
	outcome.steps = recorder.callbacks();
	if (recorder.callbacks() != horizon) {
		outcome.error = "expected " + std::to_string(horizon) + " step notifications, got "
			+ std::to_string(recorder.callbacks()) + "\n";
		return outcome;
	}

	if (!recorder.signalChecker().problems().empty()) {
		outcome.error = "the signal states of the snapshot differ from the route sections:\n";
		for (const std::string& problem : recorder.signalChecker().problems())
			outcome.error += "  " + problem + "\n";
		return outcome;
	}

	Observation& obs = outcome.observation;
	auto add = [&obs](const std::string& key, std::vector<Field> fields = {}) {
		obs.push_back({key, std::move(fields)});
	};

	add("run", {integerField("steps", horizon), integerField("trains", numRegions)});

	std::vector<const Train*> trains;
	for (int i = 0; i < numRegions; ++i)
		trains.push_back(&regional_train[i]);
	const std::vector<TrainTrack> tracks = collectTracks();

	for (const TrainTrack& track : tracks) {
		const std::string prefix = "train " + track.name;
		if (track.first > track.last) {
			add(prefix + " active", {textField("", "never")});
			continue;
		}
		const auto& x = *track.position;
		const auto& v = *track.speed;
		add(prefix + " active", {integerField("start", track.first), integerField("end", track.last)});
		std::vector<int> sampleSteps;
		for (int t = track.first; t <= track.last; t += 60)
			sampleSteps.push_back(t);
		if (sampleSteps.back() != track.last)
			sampleSteps.push_back(track.last);
		for (int t : sampleSteps)
			add(prefix + " sample", {integerField("t", t), realField("x", x[t]), realField("v", v[t])});
		for (const Stop& stop : findStops(track)) {
			add(prefix + " stop",
				{integerField("t_first", stop.first), integerField("t_last", stop.last), realField("x", stop.position),
					integerField("dwell", stop.last - stop.first)});
			int t = stop.first;
			while (t > track.first && v[t - 1] > v[t] + 1e-9)
				--t;
			if (t != stop.first)
				add(prefix + " braking", {integerField("t", t), realField("x", x[t]), realField("v", v[t]), integerField("stop_t", stop.first)});
		}
	}

	const std::vector<Separation> separations = measureSeparations(tracks);
	for (const Separation& separation : separations) {
		const std::string key = "separation " + separation.leader + ">" + separation.follower;
		if (separation.overlap)
			add(key, {integerField("min_gap_t", separation.step), realField("min_gap", separation.gap)});
		else
			add(key, {textField("", "never_together")});
	}

	for (const StepRecorder::RouteCodes& route : recorder.routes()) {
		for (size_t b = 0; b < route.sectionIds.size(); ++b) {
			Line line;
			line.key = "aspect " + train_route[route.routeIndex].ID + " " + route.sectionIds[b];
			for (const auto& change : route.changes[b]) {
				Field field;
				field.kind = Field::Kind::Text;
				const double code = change.second;
				field.text = std::to_string(change.first) + ":"
					+ (code == std::floor(code) ? std::to_string(static_cast<long long>(code)) : formatReal(code));
				line.fields.push_back(field);
			}
			obs.push_back(std::move(line));
		}
	}

	const std::vector<TimetableResultRow> rows = buildTimetableResults(trains);
	for (const TimetableResultRow& row : rows)
		add("result " + row.trainId + " " + row.stationId,
			{resultField("planned_arr", row.plannedArrivalSeconds),
				resultField("planned_dep", row.plannedDepartureSeconds),
				resultField("sim_arr", row.simulatedArrivalSeconds),
				resultField("sim_dep", row.simulatedDepartureSeconds),
				resultField("arr_delay", row.arrivalDelaySeconds),
				resultField("dep_delay", row.departureDelaySeconds)});

	const RunResults results = buildRunResults(trains, timestep);
	for (const TrainRunResult& result : results.trains)
		add("run_result " + result.trainId,
			{resultField("start", result.startSeconds), resultField("end", result.endSeconds),
				resultField("travel", result.travelSeconds)});
	add("network",
		{resultField("start", results.networkStartSeconds), resultField("end", results.networkEndSeconds),
			resultField("travel", results.networkTravelSeconds)});

	for (const Train* train : trains) {
		const std::string prefix = "train " + train->trainDescription;
		add(prefix + " direct_incident",
			{integerField("count", static_cast<long long>(train->directIncidentIds.size())),
				textField("ids", joinSet(train->directIncidentIds)), realField("first_time", train->firstDirectIncidentTime),
				realField("first_location", train->firstDirectIncidentLocation)});
		const int blockTimes = std::min(train->N_BlockSections, 1000);
		add("blocktime " + train->trainDescription, {integerField("n", blockTimes), integerField("complete", train->N_BlockTimeComplete)});
		for (int b = 0; b < blockTimes; ++b) {
			const BlockingTimes& block = train->BlockTime[b];
			add("blocktime " + train->trainDescription + " " + block.BlockID,
				{integerField("level", block.SignallingLevel), integerField("complete", block.IsComplete ? 1 : 0),
					realField("start_occ", block.StartOccTime), realField("end_occ", block.EndOccTime),
					realField("start_run", block.StartRunTime), realField("end_run", block.EndRunTime),
					realField("end_clear", block.EndClearTime)});
		}
	}

	for (const SimulationIncident& incident : simulationIncidents)
		add("incident " + incident.id,
			{textField("type", incident.type), textField("target", incident.target),
				realField("start", incident.startSeconds), integerField("has_end", incident.hasEndSeconds ? 1 : 0),
				realField("end", incident.endSeconds), textField("sections", joinSet(incident.resolvedSectionIDs))});
	for (const StepRecorder::Boundary& boundary : recorder.boundaries()) {
		add("boundary",
			{integerField("t", boundary.step), integerField("authorities", static_cast<long long>(boundary.authorities.size())),
				textField("blocked", joinSet(boundary.blocked))});
		for (const StepRecorder::Authority& authority : boundary.authorities)
			add("authority",
				{integerField("t", boundary.step), textField("part", authority.part), textField("section", authority.section),
					realField("pos", authority.position), integerField("reversed", authority.reversed ? 1 : 0)});
	}

	if (!readStationStats(outputDir.path().toStdString() + "/TrainTrajectories/Stats_Stations.txt", "stats", obs)) {
		outcome.error = "the run wrote no TrainTrajectories/Stats_Stations.txt\n";
		return outcome;
	}
	if (!readStationStats(outputDir.path().toStdString() + "/TrainTrajectories/Pos&Neg_Stats_Stations.txt", "signed_stats",
			obs)) {
		outcome.error = "the run wrote no TrainTrajectories/Pos&Neg_Stats_Stations.txt\n";
		return outcome;
	}

	if (checkInvariants) {
		outcome.invariantFailures = findInvariantViolations(spec, tracks, separations, rows);
		const std::vector<std::string> statisticsFailures = findStatisticsViolations(obs, rows);
		outcome.invariantFailures.insert(outcome.invariantFailures.end(), statisticsFailures.begin(), statisticsFailures.end());
	}
	return outcome;
}

// ---------------------------------------------------------------------------
// Golden files
// ---------------------------------------------------------------------------

std::string describeRun(const CaseSpec& spec, int horizon) {
	return "scenario=" + spec.scenario + " services=" + joinSet(spec.services)
		+ " level=" + (spec.level == kNoSignallingArea ? std::string("none") : std::to_string(spec.level))
		+ (spec.singleTrack ? " single_track=1" : "") + " horizon=" + std::to_string(horizon);
}

std::string knownWrongText(const CaseSpec& spec) {
	return spec.knownWrong.empty() ? "none" : spec.knownWrong;
}

std::string renderGolden(const CaseSpec& spec, int horizon, const Observation& observation) {
	std::string text = "# egtrain-characterization 1\n# case: " + spec.name + "\n# run: " + describeRun(spec, horizon)
		+ "\n# known-wrong: " + knownWrongText(spec) + "\n";
	for (const Line& line : observation)
		text += renderLine(line) + "\n";
	return text;
}

std::string trim(const std::string& text) {
	const size_t begin = text.find_first_not_of(" \t\r\n");
	if (begin == std::string::npos)
		return "";
	return text.substr(begin, text.find_last_not_of(" \t\r\n") - begin + 1);
}

struct Golden {
	bool found = false;
	std::string caseName;
	std::string run;
	std::string knownWrong;
	std::vector<std::string> lines;
};

Golden readGolden(const std::string& path) {
	Golden golden;
	std::ifstream in(path);
	if (!in)
		return golden;
	golden.found = true;
	std::string text;
	while (std::getline(in, text)) {
		if (!text.empty() && text.back() == '\r')
			text.pop_back();
		if (text.rfind("# case:", 0) == 0)
			golden.caseName = trim(text.substr(7));
		else if (text.rfind("# run:", 0) == 0)
			golden.run = trim(text.substr(6));
		else if (text.rfind("# known-wrong:", 0) == 0)
			golden.knownWrong = trim(text.substr(14));
		if (trim(text).empty() || trim(text)[0] == '#')
			continue;
		golden.lines.push_back(text);
	}
	return golden;
}

// Empty when equal, otherwise a description of the first difference.
std::string compareLine(const Line& actual, const std::string& expectedText) {
	const std::vector<std::string> expected = splitWords(expectedText);
	const std::vector<std::string> keyWords = splitWords(actual.key);
	std::vector<std::string> actualWords = keyWords;
	for (const Field& field : actual.fields)
		actualWords.push_back(field.name.empty() ? field.text : field.name + "=" + field.text);
	if (expected.size() != actualWords.size())
		return "number of tokens differs (" + std::to_string(expected.size()) + " expected, "
			+ std::to_string(actualWords.size()) + " actual)";
	for (size_t i = 0; i < keyWords.size(); ++i)
		if (expected[i] != keyWords[i])
			return "token " + std::to_string(i + 1) + " is " + expected[i] + ", actual " + keyWords[i];
	for (size_t i = 0; i < actual.fields.size(); ++i) {
		const Field& field = actual.fields[i];
		const std::string& token = expected[keyWords.size() + i];
		const std::string name = field.name.empty() ? "" : field.name + "=";
		if (token.compare(0, name.size(), name) != 0)
			return "field " + (field.name.empty() ? std::to_string(i + 1) : field.name) + ": expected " + token
				+ ", actual " + name + field.text;
		const std::string value = token.substr(name.size());
		const std::string label = field.name.empty() ? "token " + std::to_string(keyWords.size() + i + 1) : field.name;
		if (field.kind == Field::Kind::Float || field.kind == Field::Kind::Stat) {
			double expectedValue = 0.0;
			if (parseReal(value, expectedValue) && std::isfinite(field.real)) {
				const bool stat = field.kind == Field::Kind::Stat;
				const double tolerance = (stat ? kStatAbsTolerance : kFloatAbsTolerance)
					+ (stat ? kStatRelTolerance : kFloatRelTolerance) * std::fabs(expectedValue);
				if (std::fabs(field.real - expectedValue) <= tolerance)
					continue;
				return label + ": expected " + value + ", actual " + field.text + " (difference "
					+ formatReal(field.real - expectedValue) + ", tolerance " + std::to_string(tolerance) + ")";
			}
		}
		if (value != field.text)
			return label + ": expected " + value + ", actual " + field.text;
	}
	return "";
}

bool compareWithGolden(const CaseSpec& spec, int horizon, const Golden& golden, const Observation& observation) {
	std::vector<std::string> problems;
	if (golden.caseName != spec.name)
		problems.push_back("golden file is for case '" + golden.caseName + "', not '" + spec.name + "'");
	if (golden.run != describeRun(spec, horizon))
		problems.push_back("golden file was recorded for '" + golden.run + "', the case runs '"
			+ describeRun(spec, horizon) + "'");
	if (golden.knownWrong != knownWrongText(spec))
		problems.push_back("known-wrong marker differs: golden says '" + golden.knownWrong
			+ "', case table says '" + knownWrongText(spec) + "'");
	const size_t common = std::min(golden.lines.size(), observation.size());
	size_t mismatches = 0;
	for (size_t i = 0; i < common; ++i) {
		const std::string difference = compareLine(observation[i], golden.lines[i]);
		if (difference.empty())
			continue;
		++mismatches;
		if (mismatches <= 20)
			problems.push_back("line " + std::to_string(i + 1) + ": " + difference + "\n    expected: "
				+ golden.lines[i] + "\n    actual:   " + renderLine(observation[i]));
	}
	if (mismatches > 20)
		problems.push_back("... and " + std::to_string(mismatches - 20) + " more differing lines");
	if (golden.lines.size() != observation.size())
		problems.push_back("expected " + std::to_string(golden.lines.size()) + " lines, actual "
			+ std::to_string(observation.size()));
	for (const std::string& problem : problems)
		std::cerr << "  " << problem << "\n";
	return problems.empty();
}

bool writeText(const std::string& path, const std::string& text) {
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	out << text;
	return static_cast<bool>(out);
}

// ---------------------------------------------------------------------------
// Entry points
// ---------------------------------------------------------------------------

int runGoldenMode(const std::string& fixture, const std::string& expectDir, const std::string& caseName) {
	const std::vector<CaseSpec> cases = buildCaseTable();
	const CaseSpec* spec = findCase(cases, caseName);
	if (!spec) {
		std::cerr << "unknown case '" << caseName << "'\n";
		return 2;
	}
	const char* updateValue = std::getenv("EGTRAIN_UPDATE_EXPECTATIONS");
	const bool update = updateValue && std::string(updateValue) == "1";
	const char* ci = std::getenv("CI");
	if (update && ci && *ci) {
		std::cerr << "EGTRAIN_UPDATE_EXPECTATIONS=1 is refused when CI is set\n";
		return 1;
	}
	if (!spec->knownWrong.empty())
		std::cout << "KNOWN-WRONG " << spec->knownWrong << "\n";

	RunOutcome outcome = runCase(fixture, *spec, true);
	if (!outcome.error.empty()) {
		std::cerr << "FAIL " << caseName << ": " << outcome.error;
		return 1;
	}
	for (const std::string& failure : outcome.invariantFailures)
		std::cerr << "FAIL " << caseName << " invariant: " << failure << "\n";
	if (!outcome.invariantFailures.empty())
		return 1;

	const std::string goldenPath = expectDir + "/" + caseName + ".txt";
	const std::string rendered = renderGolden(*spec, outcome.steps, outcome.observation);
	if (update) {
		if (!writeText(goldenPath, rendered)) {
			std::cerr << "cannot write " << goldenPath << "\n";
			return 1;
		}
		std::cout << "UPDATED " << goldenPath << "\n";
		return 0;
	}
	const Golden golden = readGolden(goldenPath);
	if (!golden.found) {
		std::cerr << "FAIL " << caseName << ": missing golden file " << goldenPath
				  << "\n  run with EGTRAIN_UPDATE_EXPECTATIONS=1 to create it\n";
		return 1;
	}
	std::cerr.flush();
	if (!compareWithGolden(*spec, outcome.steps, golden, outcome.observation)) {
		const std::string actualPath = caseName + ".actual.txt";
		writeText(actualPath, rendered);
		std::cerr << "FAIL " << caseName << ": output differs from " << goldenPath << "\n  actual output written to "
				  << actualPath << "\n";
		return 1;
	}
	std::cout << "PASS " << caseName << "\n";
	return 0;
}

int runRepeatMode(const std::vector<std::string>& steps) {
	const std::vector<CaseSpec> cases = buildCaseTable();
	std::map<std::string, Observation> first;
	int repeats = 0;
	for (const std::string& step : steps) {
		const size_t split = step.rfind('#');
		const std::string sceneDir = step.substr(0, split);
		CaseSpec spec;
		spec.name = "committed";
		if (split != std::string::npos) {
			const CaseSpec* known = findCase(cases, step.substr(split + 1));
			if (!known) {
				std::cerr << "unknown case in step '" << step << "'\n";
				return 2;
			}
			spec = *known;
		}
		RunOutcome outcome = runCase(sceneDir, spec, false);
		if (!outcome.error.empty()) {
			std::cerr << "FAIL " << step << ": " << outcome.error;
			return 1;
		}
		const auto previous = first.find(step);
		if (previous == first.end()) {
			first[step] = std::move(outcome.observation);
			continue;
		}
		++repeats;
		const Observation& before = previous->second;
		const Observation& after = outcome.observation;
		size_t differences = 0;
		for (size_t i = 0; i < std::max(before.size(), after.size()); ++i) {
			if (i < before.size() && i < after.size() && sameLine(before[i], after[i]))
				continue;
			if (++differences <= 20)
				std::cerr << "  line " << i + 1 << "\n    first run:  "
						  << (i < before.size() ? renderLine(before[i]) : "(missing)") << "\n    second run: "
						  << (i < after.size() ? renderLine(after[i]) : "(missing)") << "\n";
		}
		if (differences) {
			std::cerr << "FAIL " << step << ": the second run differs from the first in " << differences
					  << " lines\n";
			return 1;
		}
	}
	if (repeats == 0) {
		std::cerr << "FAIL no step appears twice, nothing was compared\n";
		return 1;
	}
	std::cout << "PASS " << repeats << " repeated runs identical\n";
	return 0;
}

} // namespace

int main(int argc, char** argv) {
	QCoreApplication application(argc, argv);

	std::string fixture, expect, caseName;
	std::vector<std::string> repeatSteps;
	bool repeat = false;
	for (int i = 1; i < argc; ++i) {
		const std::string arg = argv[i];
		auto value = [&]() -> std::string { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
		if (arg == "--fixture")
			fixture = value();
		else if (arg == "--expect")
			expect = value();
		else if (arg == "--case")
			caseName = value();
		else if (arg == "--repeat")
			repeat = true;
		else if (repeat)
			repeatSteps.push_back(arg);
		else {
			std::cerr << "unknown argument '" << arg << "'\n";
			return 2;
		}
	}
	if (repeat && !repeatSteps.empty())
		return runRepeatMode(repeatSteps);
	if (!repeat && !fixture.empty() && !expect.empty() && !caseName.empty())
		return runGoldenMode(fixture, expect, caseName);
	std::cerr << "usage: test_characterization --fixture DIR --expect DIR --case NAME\n"
				 "       test_characterization --repeat SCENE[#CASE]...\n";
	return 2;
}
