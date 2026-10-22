// Tests of the live frame model (io/LiveFrame.h) and of the schema that describes its frames
// (docs/live/frame-v1.schema.json).
//
// Frames made from hand-built snapshots pin each member and each null rule. The line fixture then
// runs in this process, and the frames of its snapshots are compared with the golden files in
// tests/live/expected as parsed JSON, with a tolerance for numbers. Every frame is checked against
// the schema, which has to name every member that a frame has.
//
//   test_liveframe --fixture DIR --expect DIR --schema FILE
//
// EGTRAIN_UPDATE_EXPECTATIONS=1 rewrites the golden files instead of comparing; it is refused when
// CI is set.

#include "app/DispatchController.h"
#include "io/LiveFrame.h"
#include "simulation/InitialParameters.h"
#include "simulation/Infrastructure.h"
#include "simulation/Simulation.h"
#ifdef signals
#undef signals
#endif

#include <QCoreApplication>
#include <QObject>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {
using nlohmann::json;

// The fixture is one track of this length (tests/characterization).
constexpr double kFixtureLengthKm = 16.0;
// Steps of the fixture run that have a golden file: nothing has entered, only the first train is on
// the line, both are, the signal failure starts (at 400 s in the scenario), a train waits for it,
// and the run has ended.
const int kGoldenSteps[] = {0, 90, 300, 400, 420, 1499};
constexpr double kAbsTolerance = 1e-4;
constexpr double kRelTolerance = 1e-9;

int failures = 0;

void check(bool ok, const std::string& what) {
	if (ok)
		return;
	++failures;
	std::cerr << "failed: " << what << "\n";
}

// --- The schema ---

json schemaDocument;

// Checks a value against the part of JSON Schema that the schema file uses. A keyword that it does
// not know and a member that the schema does not name are errors, so every member of a frame has
// to be documented.
const char* const kKnownKeywords[] = {"$schema", "$id", "$defs", "$ref", "title", "description", "type", "const", "enum", "required",
	"properties", "items", "oneOf", "minimum", "maximum"};

bool hasType(const json& value, const std::string& type) {
	if (type == "object")
		return value.is_object();
	if (type == "array")
		return value.is_array();
	if (type == "string")
		return value.is_string();
	if (type == "boolean")
		return value.is_boolean();
	if (type == "null")
		return value.is_null();
	if (type == "integer")
		return value.is_number_integer();
	if (type == "number")
		return value.is_number();
	return false;
}

bool conforms(const json& schema, const json& value, const std::string& at, std::string& why) {
	for (auto keyword = schema.begin(); keyword != schema.end(); ++keyword)
		if (std::find(std::begin(kKnownKeywords), std::end(kKnownKeywords), keyword.key()) == std::end(kKnownKeywords)) {
			why = at + ": the checker does not know the keyword " + keyword.key();
			return false;
		}
	if (schema.contains("$ref")) {
		const std::string reference = schema.at("$ref");
		const std::string prefix = "#/$defs/";
		const std::string name = reference.compare(0, prefix.size(), prefix) == 0 ? reference.substr(prefix.size()) : std::string();
		if (!schemaDocument.at("$defs").contains(name)) {
			why = at + ": unknown reference " + reference;
			return false;
		}
		return conforms(schemaDocument.at("$defs").at(name), value, at, why);
	}
	if (schema.contains("const") && value != schema.at("const")) {
		why = at + ": not the constant " + schema.at("const").dump();
		return false;
	}
	if (schema.contains("enum") && std::find(schema.at("enum").begin(), schema.at("enum").end(), value) == schema.at("enum").end()) {
		why = at + ": not one of " + schema.at("enum").dump();
		return false;
	}
	if (schema.contains("type")) {
		const json& type = schema.at("type");
		const std::vector<std::string> types = type.is_array() ? type.get<std::vector<std::string>>() : std::vector<std::string>{type.get<std::string>()};
		if (std::none_of(types.begin(), types.end(), [&value](const std::string& name) { return hasType(value, name); })) {
			why = at + ": a " + value.type_name() + " is not " + json(types).dump();
			return false;
		}
	}
	if (value.is_number()) {
		if (schema.contains("minimum") && value.get<double>() < schema.at("minimum").get<double>()) {
			why = at + ": below the minimum";
			return false;
		}
		if (schema.contains("maximum") && value.get<double>() > schema.at("maximum").get<double>()) {
			why = at + ": above the maximum";
			return false;
		}
	}
	if (value.is_object()) {
		if (schema.contains("required"))
			for (const json& name : schema.at("required"))
				if (!value.contains(name.get<std::string>())) {
					why = at + ": missing " + name.get<std::string>();
					return false;
				}
		if (schema.contains("properties"))
			for (auto member = value.begin(); member != value.end(); ++member) {
				if (!schema.at("properties").contains(member.key())) {
					why = at + ": the schema does not describe " + member.key();
					return false;
				}
				if (!conforms(schema.at("properties").at(member.key()), member.value(), at + "." + member.key(), why))
					return false;
			}
	}
	if (value.is_array() && schema.contains("items"))
		for (std::size_t i = 0; i < value.size(); ++i)
			if (!conforms(schema.at("items"), value[i], at + "[" + std::to_string(i) + "]", why))
				return false;
	if (schema.contains("oneOf")) {
		int matches = 0;
		std::string reasons;
		for (const json& alternative : schema.at("oneOf")) {
			std::string reason;
			if (conforms(alternative, value, at, reason))
				++matches;
			else
				reasons += " [" + reason + "]";
		}
		if (matches != 1) {
			why = at + ": " + std::to_string(matches) + " alternatives match:" + reasons;
			return false;
		}
	}
	return true;
}

// Empty when the frame conforms to the schema, else what is wrong with it.
std::string schemaProblem(const json& frame) {
	std::string why;
	conforms(schemaDocument, frame, "frame", why);
	return why;
}

// A frame must conform to the schema, survive a trip through its text, and carry the run identity.
void checkFrame(const json& frame, const std::string& what) {
	const std::string problem = schemaProblem(frame);
	check(problem.empty(), what + " does not conform to the schema: " + problem);
	check(json::parse(frame.dump()) == frame, what + " changes in its text form");
}

// --- Comparison with a golden file ---

// Numbers compare within the tolerance when either is not an integer; everything else compares equal.
bool sameJson(const json& expected, const json& actual, const std::string& at, std::string& why) {
	if (expected.is_number() && actual.is_number()) {
		const bool integers = expected.is_number_integer() && actual.is_number_integer();
		const double difference = std::fabs(expected.get<double>() - actual.get<double>());
		if (integers ? expected == actual : difference <= kAbsTolerance + kRelTolerance * std::fabs(expected.get<double>()))
			return true;
		why = at + ": expected " + expected.dump() + ", got " + actual.dump();
		return false;
	}
	if (expected.type() != actual.type()) {
		why = at + ": expected a " + expected.type_name() + ", got a " + actual.type_name();
		return false;
	}
	if (expected.is_object()) {
		for (auto member = expected.begin(); member != expected.end(); ++member) {
			if (!actual.contains(member.key())) {
				why = at + ": missing " + member.key();
				return false;
			}
			if (!sameJson(member.value(), actual.at(member.key()), at + "." + member.key(), why))
				return false;
		}
		for (auto member = actual.begin(); member != actual.end(); ++member)
			if (!expected.contains(member.key())) {
				why = at + ": unexpected " + member.key();
				return false;
			}
		return true;
	}
	if (expected.is_array()) {
		if (expected.size() != actual.size()) {
			why = at + ": expected " + std::to_string(expected.size()) + " items, got " + std::to_string(actual.size());
			return false;
		}
		for (std::size_t i = 0; i < expected.size(); ++i)
			if (!sameJson(expected[i], actual[i], at + "[" + std::to_string(i) + "]", why))
				return false;
		return true;
	}
	if (expected == actual)
		return true;
	why = at + ": expected " + expected.dump() + ", got " + actual.dump();
	return false;
}

// Golden files hold numbers to six decimals, so that a rewrite on another platform does not change them.
json rounded(const json& value) {
	if (value.is_number_float())
		return std::round(value.get<double>() * 1e6) / 1e6;
	json copy = value;
	if (copy.is_array() || copy.is_object())
		for (json& item : copy) item = rounded(item);
	return copy;
}

bool updating = false;

void checkGolden(const std::string& directory, const std::string& name, const json& actual) {
	const std::string path = directory + "/" + name;
	if (updating) {
		std::ofstream out(path);
		out << rounded(actual).dump(2) << "\n";
		check(out.good(), "cannot write " + path);
		return;
	}
	std::ifstream in(path);
	json expected;
	try {
		expected = json::parse(in);
	} catch (const json::parse_error&) {
		check(false, "cannot read the golden file " + path + "; EGTRAIN_UPDATE_EXPECTATIONS=1 creates it");
		return;
	}
	std::string why;
	check(sameJson(expected, actual, name, why), "the frames differ from " + path + ": " + why);
}

// --- Frames of hand-built snapshots ---

LiveRunInfo sampleRun() {
	LiveRunInfo run;
	run.runId = "run-1";
	run.sceneName = "Sample";
	run.baseTimeSeconds = 25200.0;
	run.horizonSteps = 600;
	run.stepSeconds = 2.0;
	return run;
}

// A train that has entered and has a position at step 100.
GuiTrainState locatedTrain() {
	GuiTrainState train;
	train.serviceId = "S1";
	train.occurrence = 3;
	train.operatingCode = "IC 1";
	train.routeIndex = 2;
	train.departureTime = 60;
	train.routeAxisPosition = 1234.5;
	train.speedKmh = 72.0;
	train.wagonHeadPositions = {12.5};
	train.wagonTailPositions = {12.25};
	train.currentOnboardPassengers = 17;
	return train;
}

bool isNumber(const json& value, double expected) {
	return value.is_number() && std::fabs(value.get<double>() - expected) < 1e-9;
}

void testStateFrame() {
	GuiSimulationSnapshot snapshot;
	snapshot.timestep = 100;
	GuiTrainState early = locatedTrain();
	early.serviceId = "S2";
	early.departureTime = 200;
	GuiTrainState left = locatedTrain();
	left.serviceId = "S3";
	left.outOfSimulation = true;
	GuiTrainState unlocated;
	unlocated.serviceId = "S4";
	unlocated.reversedDirection = true;
	GuiTrainState noX = locatedTrain();
	noX.serviceId = "S5";
	noX.wagonHeadPositions = {-1.0};
	GuiTrainState notANumber = locatedTrain();
	notANumber.serviceId = "S6";
	notANumber.speedKmh = std::numeric_limits<double>::quiet_NaN();
	snapshot.trains = {locatedTrain(), early, left, unlocated, noX, notANumber};

	const json frame = makeStateFrame(sampleRun(), 7, snapshot, false);
	checkFrame(frame, "state frame");
	check(frame.at("v") == kLiveContractVersion && frame.at("run_id") == "run-1" && frame.at("seq") == 7, "state frame identity");
	check(frame.at("step") == 100 && isNumber(frame.at("sim_time_s"), 200.0) && isNumber(frame.at("clock_s"), 25400.0),
		"state frame time is the step times the step length, from the base time");
	check(!frame.contains("signals"), "state frame without signals has a signal list");
	check(frame.at("trains").size() == 6, "state frame lists every train");

	const json& trains = frame.at("trains");
	check(trains[0].at("service_id") == "S1" && trains[0].at("occurrence") == 3 && trains[0].at("operating_code") == "IC 1", "train identity");
	check(trains[0].at("in_network") == true && trains[0].at("direction") == "forward" && trains[0].at("onboard") == 17, "train flags");
	check(isNumber(trains[0].at("route_pos_m"), 1234.5) && isNumber(trains[0].at("x_km"), 12.5) && isNumber(trains[0].at("speed_ms"), 20.0),
		"train position and speed in the units of the contract");

	check(trains[1].at("in_network") == false && trains[1].at("route_pos_m").is_null() && trains[1].at("x_km").is_null()
			&& trains[1].at("speed_ms").is_null(),
		"a train that is not due has no position");
	check(trains[2].at("in_network") == false && trains[2].at("route_pos_m").is_null() && trains[2].at("speed_ms").is_null(),
		"a train that has left has no position");
	check(trains[3].at("in_network") == true && trains[3].at("direction") == "reversed" && trains[3].at("route_pos_m").is_null()
			&& trains[3].at("x_km").is_null() && trains[3].at("speed_ms").is_null(),
		"the position sentinel does not leave the process");
	check(isNumber(trains[4].at("route_pos_m"), 1234.5) && trains[4].at("x_km").is_null(), "a head outside every block section has no x");
	check(trains[5].at("speed_ms").is_null(), "a speed that is not a number is null");
}

void testSignals() {
	GuiSimulationSnapshot snapshot;
	snapshot.signalStates = {{"1-B0", 75, false, 2, false}, {"2-B0", 270, true, kGuiSignalNoLevel, false}, {"3-B0", 0, false, 1, true}};

	const json frame = makeStateFrame(sampleRun(), 1, snapshot, true);
	checkFrame(frame, "state frame with signals");
	check(frame.at("signals").size() == 3, "signal list length");
	const json& list = frame.at("signals");
	check(list[0].at("section_id") == "1-B0" && list[0].at("code") == 75 && list[0].at("level") == 2
			&& list[0].at("failed") == false && list[0].at("reversed") == false,
		"signal with a level");
	check(list[1].at("code").is_null() && list[1].at("level").is_null() && list[1].at("reversed") == true,
		"a section without a level has no code, whatever the initial code is");
	check(list[2].at("code") == 0 && list[2].at("failed") == true, "failed signal");
	check(!makeStateFrame(sampleRun(), 1, snapshot, false).contains("signals"), "signals left out");
}

void testSignalsChanged() {
	const std::vector<GuiSignalState> base = {{"1-B0", 75, false, 2, false}, {"2-B0", 270, true, kGuiSignalNoLevel, false}};
	check(!liveSignalsChanged({}, {}) && !liveSignalsChanged(base, base), "equal lists changed");
	check(liveSignalsChanged({}, base) && liveSignalsChanged(base, {}), "lists of another length did not change");
	const struct {
		const char* name;
		void (*apply)(std::vector<GuiSignalState>&);
		bool changes;
	} edits[] = {
		{"code of a section with a level", [](std::vector<GuiSignalState>& s) { s[0].code = 180; }, true},
		{"level", [](std::vector<GuiSignalState>& s) { s[0].level = 3; }, true},
		{"failure", [](std::vector<GuiSignalState>& s) { s[0].failed = true; }, true},
		{"direction", [](std::vector<GuiSignalState>& s) { s[0].reversedDirection = true; }, true},
		{"section id", [](std::vector<GuiSignalState>& s) { s[1].sectionId = "3-B0"; }, true},
		{"order", [](std::vector<GuiSignalState>& s) { std::swap(s[0], s[1]); }, true},
		{"code of a section without a level", [](std::vector<GuiSignalState>& s) { s[1].code = 0; }, false},
	};
	for (const auto& edit : edits) {
		std::vector<GuiSignalState> changed = base;
		edit.apply(changed);
		check(liveSignalsChanged(base, changed) == edit.changes, std::string("a change of the ") + edit.name);
	}
}

void testHelloFrame() {
	GuiSimulationSnapshot snapshot;
	GuiTrainState second = locatedTrain();
	second.serviceId = "S2";
	second.occurrence = 1;
	second.operatingCode.clear();
	second.routeIndex = 0;
	snapshot.trains = {locatedTrain(), second};

	const json frame = makeHelloFrame(sampleRun(), 0, snapshot);
	checkFrame(frame, "hello frame");
	check(frame.at("v") == 1 && frame.at("run_id") == "run-1" && frame.at("seq") == 0, "hello frame identity");
	check(frame.at("scene_name") == "Sample" && isNumber(frame.at("base_time_s"), 25200.0) && frame.at("horizon_steps") == 600
			&& isNumber(frame.at("step_s"), 2.0),
		"hello frame time axis");
	check(frame.at("units") == json({{"distance", "m"}, {"time", "s"}, {"speed", "m/s"}, {"x", "km"}}), "hello frame units");
	const json& trains = frame.at("trains");
	check(trains.size() == 2 && trains[0].at("route_index") == 2 && trains[1].at("service_id") == "S2" && trains[1].at("operating_code") == ""
			&& trains[1].at("route_index") == 0 && trains[1].at("occurrence") == 1,
		"hello frame trains");
}

void testEventFrames() {
	const LiveRunInfo run = sampleRun();
	const json finished = makeEventFrame(run, 9, LiveEvent::StepsFinished, 600);
	const json completed = makeEventFrame(run, 10, LiveEvent::RunCompleted, 600);
	const json cancelled = makeEventFrame(run, 5, LiveEvent::RunCancelled, 100);
	for (const json* frame : {&finished, &completed, &cancelled}) checkFrame(*frame, "event frame");
	check(finished.at("kind") == "steps_finished" && finished.at("step") == 600 && finished.at("seq") == 9 && !finished.contains("status"),
		"steps_finished");
	check(completed.at("kind") == "run_finished" && completed.at("status") == "completed" && completed.at("run_id") == "run-1", "run_finished completed");
	check(cancelled.at("kind") == "run_finished" && cancelled.at("status") == "cancelled" && cancelled.at("step") == 100, "run_finished cancelled");
}

void testCommandEventFrames() {
	const LiveRunInfo run = sampleRun();
	LiveCommand command;
	command.kind = LiveCommandKind::SetDelayMs;
	command.id = "client-1";
	command.delayMs = 20;
	const json applied = makeCommandEventFrame(run, 4, command, 90);
	const json rejected = makeCommandEventFrame(run, 5, command, 600, "run_ended");
	for (const json* frame : {&applied, &rejected}) checkFrame(*frame, "command event frame");
	check(applied.at("kind") == "applied" && applied.at("step") == 90 && applied.at("command_id") == "client-1" && applied.at("cmd") == "set_delay_ms"
			&& !applied.contains("reason"),
		"applied command");
	check(rejected.at("kind") == "rejected" && rejected.at("reason") == "run_ended" && rejected.at("seq") == 5 && rejected.at("run_id") == "run-1",
		"rejected command");
	for (const LiveCommandKind kind : {LiveCommandKind::Pause, LiveCommandKind::Resume, LiveCommandKind::Stop}) {
		command.kind = kind;
		checkFrame(makeCommandEventFrame(run, 6, command, 1), "command event frame of " + std::string(liveCommandName(kind)));
	}
	for (const char* reason : {"run_ended", "no_active_run", "queue_full"})
		checkFrame(makeCommandEventFrame(run, 7, command, 0, reason), std::string("command event frame rejected for ") + reason);
}

// The checker has to reject what the schema forbids, or the checks above prove nothing.
void testSchemaRejects() {
	GuiSimulationSnapshot snapshot;
	snapshot.trains = {locatedTrain()};
	snapshot.signalStates = {{"1-B0", 75, false, 2, false}};
	const json good = makeStateFrame(sampleRun(), 1, snapshot, true);
	check(schemaProblem(good).empty(), "the sample frame does not conform");

	const struct {
		const char* name;
		void (*apply)(json&);
	} faults[] = {
		{"a missing member", [](json& f) { f.erase("clock_s"); }},
		{"an undocumented member", [](json& f) { f["extra"] = 1; }},
		{"another version", [](json& f) { f["v"] = 2; }},
		{"a negative seq", [](json& f) { f["seq"] = -1; }},
		{"a text step", [](json& f) { f["step"] = "1"; }},
		{"a position that is not a number", [](json& f) { f["trains"][0]["route_pos_m"] = "far"; }},
		{"a missing train member", [](json& f) { f["trains"][0].erase("onboard"); }},
		{"a direction outside the list", [](json& f) { f["trains"][0]["direction"] = "up"; }},
		{"a level above 5", [](json& f) { f["signals"][0]["level"] = 6; }},
		{"an undocumented signal member", [](json& f) { f["signals"][0]["aspect"] = 1; }},
	};
	for (const auto& fault : faults) {
		json broken = good;
		fault.apply(broken);
		check(!schemaProblem(broken).empty(), std::string("the schema accepts ") + fault.name);
	}
	check(!schemaProblem(json::object()).empty(), "the schema accepts an empty object");
	json steps = makeEventFrame(sampleRun(), 1, LiveEvent::StepsFinished, 3);
	steps["status"] = "completed";
	check(!schemaProblem(steps).empty(), "the schema accepts a status on steps_finished");

	LiveCommand command;
	command.id = "c1";
	const json applied = makeCommandEventFrame(sampleRun(), 1, command, 3);
	const struct {
		const char* name;
		void (*apply)(json&);
	} commandFaults[] = {
		{"a command event without command_id", [](json& f) { f.erase("command_id"); }},
		{"a command event with a command the contract does not have", [](json& f) { f["cmd"] = "set_speed"; }},
		{"a command event with an unknown reason", [](json& f) { f["reason"] = "late"; }},
		{"a command event with an unknown member", [](json& f) { f["accepted_at"] = 1; }},
		{"a command event with a negative step", [](json& f) { f["step"] = -1; }},
	};
	for (const auto& fault : commandFaults) {
		json broken = applied;
		fault.apply(broken);
		check(!schemaProblem(broken).empty(), std::string("the schema accepts ") + fault.name);
	}
}

// --- The line fixture ---

class CoutSilencer {
public:
	CoutSilencer() : saved_(std::cout.rdbuf(sink_.rdbuf())) {}
	~CoutSilencer() { std::cout.rdbuf(saved_); }

private:
	std::ostringstream sink_;
	std::streambuf* saved_;
};

struct FixtureRun {
	LiveRunInfo info;
	std::vector<std::shared_ptr<const GuiSimulationSnapshot>> snapshots;
};

// Runs two trains of the fixture through a signal failure, with one signalling area over the line,
// and keeps the snapshot of every step.
bool runFixture(const std::string& fixture, const std::string& outputDir, FixtureRun& run) {
	SceneLoadResult loaded = loadScene(fixture);
	if (hasErrors(loaded.diagnostics)) {
		std::cerr << "cannot load " << fixture << "\n";
		return false;
	}
	SceneSignallingArea area;
	area.id = "area.all";
	area.startKm = 0.0;
	area.endKm = kFixtureLengthKm;
	area.level = 0;
	loaded.scene.signallingAreas = {area};
	SceneRunSelection selection;
	selection.insert({"F1", 1});
	selection.insert({"F2", 1});

	initial_variables.GUI = 0;
	initial_variables.PAX_GUI = false;
	initial_variables.exportDetailedTrajectories = false;
	initial_variables.OutputMainFolder = outputDir;
	initial_variables.InputMainFolder.clear();

	CoutSilencer silencer;
	if (hasErrors(simulation.prepareScene(loaded.scene, "signal-failure-forward", selection))) {
		std::cerr << "cannot prepare " << fixture << "\n";
		return false;
	}
	run.info.runId = "line-run";
	run.info.sceneName = loaded.scene.name;
	run.info.baseTimeSeconds = initial_variables.startingSimulationTime;
	run.info.horizonSteps = static_cast<int>(initial_variables.times);
	run.info.stepSeconds = timestep;

	QObject receiver;
	const auto keepSnapshot = [&run]() {
		if (auto snapshot = simulation.takeSimulationSnapshot())
			run.snapshots.push_back(std::move(snapshot));
	};
	QObject::connect(&simulation, &DispatchController::snapshotAvailable, &receiver, keepSnapshot, Qt::DirectConnection);
	simulation.runSimulation();
	QObject::disconnect(&simulation, &DispatchController::snapshotAvailable, &receiver, nullptr);
	return true;
}

struct RunFrames {
	json hello;
	std::vector<json> states;
	std::vector<json> events;
	std::size_t statesWithSignals = 0;
};

// The frames that a sender makes for the run: hello, one state frame per step with the signals
// when they changed, then the two events.
RunFrames framesOf(const FixtureRun& run) {
	RunFrames frames;
	std::uint64_t seq = 0;
	frames.hello = makeHelloFrame(run.info, seq++, *run.snapshots.front());
	const std::vector<GuiSignalState>* previous = nullptr;
	for (const auto& snapshot : run.snapshots) {
		const bool withSignals = !previous || liveSignalsChanged(*previous, snapshot->signalStates);
		frames.states.push_back(makeStateFrame(run.info, seq++, *snapshot, withSignals));
		frames.statesWithSignals += withSignals ? 1 : 0;
		previous = &snapshot->signalStates;
	}
	const int steps = static_cast<int>(run.snapshots.size());
	frames.events.push_back(makeEventFrame(run.info, seq++, LiveEvent::StepsFinished, steps));
	frames.events.push_back(makeEventFrame(run.info, seq++, LiveEvent::RunCompleted, steps));
	return frames;
}

void testFixtureRun(const std::string& fixture, const std::string& expectDir) {
	QTemporaryDir output;
	FixtureRun run;
	if (!output.isValid() || !runFixture(fixture, output.path().toStdString(), run)) {
		check(false, "the fixture did not run");
		return;
	}
	check(run.info.stepSeconds == 1.0 && run.info.horizonSteps == 1500 && run.info.baseTimeSeconds == 25200.0
			&& run.info.sceneName == "Characterization Line",
		"the run settings are not those of the fixture");
	check(static_cast<int>(run.snapshots.size()) == run.info.horizonSteps, "the run did not give one snapshot per step");
	if (static_cast<int>(run.snapshots.size()) != run.info.horizonSteps)
		return;
	for (std::size_t i = 0; i < run.snapshots.size(); ++i)
		check(run.snapshots[i]->timestep == static_cast<int>(i), "snapshot " + std::to_string(i) + " is not that of its step");

	const RunFrames frames = framesOf(run);
	checkFrame(frames.hello, "hello frame of the run");
	std::vector<const json*> inOrder = {&frames.hello};
	for (const json& state : frames.states) inOrder.push_back(&state);
	for (const json& event : frames.events) inOrder.push_back(&event);
	for (std::size_t i = 0; i < inOrder.size(); ++i)
		check(inOrder[i]->at("seq") == i && inOrder[i]->at("run_id") == "line-run" && inOrder[i]->at("v") == kLiveContractVersion,
			"frame " + std::to_string(i) + " does not carry the version, the run and the position it should");

	bool moving = false, sawFailure = false, sawUnentered = false, sawLevel = false;
	for (const json& state : frames.states) {
		checkFrame(state, "state frame " + std::to_string(state.at("step").get<int>()));
		for (const json& train : state.at("trains")) {
			moving = moving || (train.at("speed_ms").is_number() && train.at("speed_ms").get<double>() > 1.0);
			sawUnentered = sawUnentered || (!train.at("in_network").get<bool>() && train.at("route_pos_m").is_null());
		}
		if (state.contains("signals"))
			for (const json& signal : state.at("signals")) {
				sawFailure = sawFailure || signal.at("failed").get<bool>();
				sawLevel = sawLevel || !signal.at("level").is_null();
			}
	}
	for (const json& event : frames.events) checkFrame(event, "event frame");
	check(moving && sawUnentered && sawFailure && sawLevel, "the run does not show a moving train, an unentered train, a failed signal and a level");
	check(frames.states.front().contains("signals") && frames.statesWithSignals > 1 && frames.statesWithSignals < frames.states.size(),
		"the signals are sent with the first frame and again only when they change");
	check(frames.states.back().at("step") == run.info.horizonSteps - 1, "the last state frame is not that of the last step");
	const json& failureStart = frames.states[400];
	const auto failed = [](const json& signal) { return signal.at("failed") == true; };
	check(failureStart.contains("signals") && std::any_of(failureStart.at("signals").begin(), failureStart.at("signals").end(), failed),
		"the frame of the first step of the failure does not report a failed signal");

	checkGolden(expectDir, "hello.json", frames.hello);
	for (const int step : kGoldenSteps)
		checkGolden(expectDir, "state-" + std::to_string(step) + ".json", frames.states[static_cast<std::size_t>(step)]);
	checkGolden(expectDir, "events.json", json(frames.events));
}
} // namespace

int main(int argc, char** argv) {
	QCoreApplication application(argc, argv);
	std::string fixture, expectDir, schemaPath;
	for (int i = 1; i + 1 < argc; i += 2) {
		const std::string option = argv[i];
		if (option == "--fixture")
			fixture = argv[i + 1];
		else if (option == "--expect")
			expectDir = argv[i + 1];
		else if (option == "--schema")
			schemaPath = argv[i + 1];
	}
	if (fixture.empty() || expectDir.empty() || schemaPath.empty()) {
		std::cerr << "usage: test_liveframe --fixture DIR --expect DIR --schema FILE\n";
		return 2;
	}
	const char* update = std::getenv("EGTRAIN_UPDATE_EXPECTATIONS");
	updating = update && std::string(update) == "1";
	const char* ci = std::getenv("CI");
	if (updating && ci && *ci) {
		std::cerr << "EGTRAIN_UPDATE_EXPECTATIONS=1 is refused when CI is set\n";
		return 1;
	}
	try {
		std::ifstream in(schemaPath);
		schemaDocument = json::parse(in);
	} catch (const json::parse_error&) {
		std::cerr << "cannot read the schema " << schemaPath << "\n";
		return 2;
	}
	if (updating)
		std::filesystem::create_directories(expectDir);

	try {
		testStateFrame();
		testSignals();
		testSignalsChanged();
		testHelloFrame();
		testEventFrames();
		testCommandEventFrames();
		testSchemaRejects();
		testFixtureRun(fixture, expectDir);
	} catch (const std::exception& error) {
		// A missing member of a frame, for example, makes at() throw.
		std::cerr << "failed: " << error.what() << "\n";
		return 1;
	}

	if (failures)
		std::cerr << failures << " checks failed\n";
	return failures ? 1 : 0;
}
