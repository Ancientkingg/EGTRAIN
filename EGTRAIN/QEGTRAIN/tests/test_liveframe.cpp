// Tests of the live frame model (io/LiveFrame.h) and of the schema that describes its frames
// (docs/live/frame-v1.schema.json).
//
// Frames made from hand-built snapshots pin each member and each null rule. The line fixture then
// runs in this process, and the frames of its snapshots are compared with the golden files in
// tests/live/expected as parsed JSON, with a tolerance for numbers. Every frame is checked against
// the schema, which has to name every member that a frame has. The fixture is tests/fixtures/scenes/line.
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
#include <QTest>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
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
const std::string kFixtureDir = EGTRAIN_TEST_SOURCE_DIR "/fixtures/scenes/line";
const std::string kExpectDir = EGTRAIN_TEST_SOURCE_DIR "/live/expected";

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

// A frame must conform to the schema and survive a trip through its text. Empty when it does, else what is wrong with
// the frame that what names.
std::string frameProblem(const json& frame, const std::string& what) {
	const std::string problem = schemaProblem(frame);
	if (!problem.empty())
		return what + " does not conform to the schema: " + problem;
	if (json::parse(frame.dump()) != frame)
		return what + " changes in its text form";
	return {};
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

// Empty when the frames match the golden file, else how they differ. In the update mode the frames replace the file.
std::string goldenProblem(const std::string& directory, const std::string& name, const json& actual) {
	const std::string path = directory + "/" + name;
	if (updating) {
		std::ofstream out(path);
		out << rounded(actual).dump(2) << "\n";
		return out.good() ? std::string() : "cannot write " + path;
	}
	std::ifstream in(path);
	json expected;
	try {
		expected = json::parse(in);
	} catch (const json::parse_error&) {
		return "cannot read the golden file " + path + "; EGTRAIN_UPDATE_EXPECTATIONS=1 creates it";
	}
	std::string why;
	if (sameJson(expected, actual, name, why))
		return {};
	return "the frames differ from " + path + ": " + why;
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

// The rows of the golden files that do not hold a state frame.
constexpr int kHelloFile = -1;
constexpr int kEventsFile = -2;
} // namespace

class TestLiveFrame : public QObject {
	Q_OBJECT

private slots:
	void initTestCase();
	void stateFrame();
	void signalList();
	void signalListChanges();
	void helloFrame();
	void eventFrames();
	void commandEventFrames();
	void schemaRejectsFaults();
	void fixtureRun();
	void fixtureRunMatchesGoldenFile_data();
	void fixtureRunMatchesGoldenFile();

private:
	bool fixtureRan();

	std::optional<bool> m_fixtureRan;
	FixtureRun m_run;
	RunFrames m_frames;
};

void TestLiveFrame::initTestCase() {
	const char* update = std::getenv("EGTRAIN_UPDATE_EXPECTATIONS");
	updating = update && std::string(update) == "1";
	const char* ci = std::getenv("CI");
	QVERIFY2(!updating || !ci || !*ci, "EGTRAIN_UPDATE_EXPECTATIONS=1 is refused when CI is set");
	try {
		std::ifstream in(EGTRAIN_LIVE_SCHEMA);
		schemaDocument = json::parse(in);
	} catch (const json::parse_error&) {
		QFAIL("cannot read the schema " EGTRAIN_LIVE_SCHEMA);
	}
	if (updating)
		std::filesystem::create_directories(kExpectDir);
}

// Runs the line fixture when a slot first needs it, and makes its frames when it gave one snapshot per step.
bool TestLiveFrame::fixtureRan() {
	if (!m_fixtureRan) {
		QTemporaryDir output;
		m_fixtureRan = output.isValid() && runFixture(kFixtureDir, output.path().toStdString(), m_run);
		if (*m_fixtureRan && static_cast<int>(m_run.snapshots.size()) == m_run.info.horizonSteps)
			m_frames = framesOf(m_run);
	}
	return *m_fixtureRan;
}

void TestLiveFrame::stateFrame() {
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
	const std::string problem = frameProblem(frame, "state frame");
	QVERIFY2(problem.empty(), problem.c_str());
	QVERIFY2(frame.at("v") == kLiveContractVersion && frame.at("run_id") == "run-1" && frame.at("seq") == 7, "state frame identity");
	QVERIFY2(frame.at("step") == 100 && isNumber(frame.at("sim_time_s"), 200.0) && isNumber(frame.at("clock_s"), 25400.0),
		"state frame time is the step times the step length, from the base time");
	QVERIFY2(!frame.contains("signals"), "state frame without signals has a signal list");
	// The state frame lists every train.
	QCOMPARE(frame.at("trains").size(), std::size_t(6));

	const json& trains = frame.at("trains");
	QVERIFY2(trains[0].at("service_id") == "S1" && trains[0].at("occurrence") == 3 && trains[0].at("operating_code") == "IC 1", "train identity");
	QVERIFY2(trains[0].at("in_network") == true && trains[0].at("direction") == "forward" && trains[0].at("onboard") == 17, "train flags");
	QVERIFY2(isNumber(trains[0].at("route_pos_m"), 1234.5) && isNumber(trains[0].at("x_km"), 12.5) && isNumber(trains[0].at("speed_ms"), 20.0),
		"train position and speed in the units of the contract");

	QVERIFY2(trains[1].at("in_network") == false && trains[1].at("route_pos_m").is_null() && trains[1].at("x_km").is_null()
			&& trains[1].at("speed_ms").is_null(),
		"a train that is not due has no position");
	QVERIFY2(trains[2].at("in_network") == false && trains[2].at("route_pos_m").is_null() && trains[2].at("speed_ms").is_null(),
		"a train that has left has no position");
	QVERIFY2(trains[3].at("in_network") == true && trains[3].at("direction") == "reversed" && trains[3].at("route_pos_m").is_null()
			&& trains[3].at("x_km").is_null() && trains[3].at("speed_ms").is_null(),
		"the position sentinel does not leave the process");
	QVERIFY2(isNumber(trains[4].at("route_pos_m"), 1234.5) && trains[4].at("x_km").is_null(), "a head outside every block section has no x");
	QVERIFY2(trains[5].at("speed_ms").is_null(), "a speed that is not a number is null");
}

void TestLiveFrame::signalList() {
	GuiSimulationSnapshot snapshot;
	snapshot.signalStates = {{"1-B0", 75, false, 2, false}, {"2-B0", 270, true, kGuiSignalNoLevel, false}, {"3-B0", 0, false, 1, true}};

	const json frame = makeStateFrame(sampleRun(), 1, snapshot, true);
	const std::string problem = frameProblem(frame, "state frame with signals");
	QVERIFY2(problem.empty(), problem.c_str());
	// The signal list has one entry per signal.
	QCOMPARE(frame.at("signals").size(), std::size_t(3));
	const json& list = frame.at("signals");
	QVERIFY2(list[0].at("section_id") == "1-B0" && list[0].at("code") == 75 && list[0].at("level") == 2
			&& list[0].at("failed") == false && list[0].at("reversed") == false,
		"signal with a level");
	QVERIFY2(list[1].at("code").is_null() && list[1].at("level").is_null() && list[1].at("reversed") == true,
		"a section without a level has no code, whatever the initial code is");
	QVERIFY2(list[2].at("code") == 0 && list[2].at("failed") == true, "failed signal");
	QVERIFY2(!makeStateFrame(sampleRun(), 1, snapshot, false).contains("signals"), "signals left out");
}

void TestLiveFrame::signalListChanges() {
	const std::vector<GuiSignalState> base = {{"1-B0", 75, false, 2, false}, {"2-B0", 270, true, kGuiSignalNoLevel, false}};
	QVERIFY2(!liveSignalsChanged({}, {}) && !liveSignalsChanged(base, base), "equal lists changed");
	QVERIFY2(liveSignalsChanged({}, base) && liveSignalsChanged(base, {}), "lists of another length did not change");
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
		QVERIFY2(liveSignalsChanged(base, changed) == edit.changes, (std::string("a change of the ") + edit.name).c_str());
	}
}

void TestLiveFrame::helloFrame() {
	GuiSimulationSnapshot snapshot;
	GuiTrainState second = locatedTrain();
	second.serviceId = "S2";
	second.occurrence = 1;
	second.operatingCode.clear();
	second.routeIndex = 0;
	snapshot.trains = {locatedTrain(), second};

	const json frame = makeHelloFrame(sampleRun(), 0, snapshot);
	const std::string problem = frameProblem(frame, "hello frame");
	QVERIFY2(problem.empty(), problem.c_str());
	QVERIFY2(frame.at("v") == 1 && frame.at("run_id") == "run-1" && frame.at("seq") == 0, "hello frame identity");
	QVERIFY2(frame.at("scene_name") == "Sample" && isNumber(frame.at("base_time_s"), 25200.0) && frame.at("horizon_steps") == 600
			&& isNumber(frame.at("step_s"), 2.0),
		"hello frame time axis");
	QVERIFY2(frame.at("units") == json({{"distance", "m"}, {"time", "s"}, {"speed", "m/s"}, {"x", "km"}}), "hello frame units");
	const json& trains = frame.at("trains");
	QVERIFY2(trains.size() == 2 && trains[0].at("route_index") == 2 && trains[1].at("service_id") == "S2" && trains[1].at("operating_code") == ""
			&& trains[1].at("route_index") == 0 && trains[1].at("occurrence") == 1,
		"hello frame trains");
}

void TestLiveFrame::eventFrames() {
	const LiveRunInfo run = sampleRun();
	const json finished = makeEventFrame(run, 9, LiveEvent::StepsFinished, 600);
	const json completed = makeEventFrame(run, 10, LiveEvent::RunCompleted, 600);
	const json cancelled = makeEventFrame(run, 5, LiveEvent::RunCancelled, 100);
	for (const json* frame : {&finished, &completed, &cancelled}) {
		const std::string problem = frameProblem(*frame, "event frame");
		QVERIFY2(problem.empty(), problem.c_str());
	}
	QVERIFY2(finished.at("kind") == "steps_finished" && finished.at("step") == 600 && finished.at("seq") == 9 && !finished.contains("status"),
		"steps_finished");
	QVERIFY2(completed.at("kind") == "run_finished" && completed.at("status") == "completed" && completed.at("run_id") == "run-1", "run_finished completed");
	QVERIFY2(cancelled.at("kind") == "run_finished" && cancelled.at("status") == "cancelled" && cancelled.at("step") == 100, "run_finished cancelled");
}

void TestLiveFrame::commandEventFrames() {
	const LiveRunInfo run = sampleRun();
	LiveCommand command;
	command.kind = LiveCommandKind::SetDelayMs;
	command.id = "client-1";
	command.delayMs = 20;
	const json applied = makeCommandEventFrame(run, 4, command, 90);
	const json rejected = makeCommandEventFrame(run, 5, command, 600, "run_ended");
	for (const json* frame : {&applied, &rejected}) {
		const std::string problem = frameProblem(*frame, "command event frame");
		QVERIFY2(problem.empty(), problem.c_str());
	}
	QVERIFY2(applied.at("kind") == "applied" && applied.at("step") == 90 && applied.at("command_id") == "client-1" && applied.at("cmd") == "set_delay_ms"
			&& !applied.contains("reason"),
		"applied command");
	QVERIFY2(rejected.at("kind") == "rejected" && rejected.at("reason") == "run_ended" && rejected.at("seq") == 5 && rejected.at("run_id") == "run-1",
		"rejected command");
	for (const LiveCommandKind kind : {LiveCommandKind::Pause, LiveCommandKind::Resume, LiveCommandKind::Stop}) {
		command.kind = kind;
		const std::string problem = frameProblem(makeCommandEventFrame(run, 6, command, 1), "command event frame of " + std::string(liveCommandName(kind)));
		QVERIFY2(problem.empty(), problem.c_str());
	}
	for (const char* reason : {"run_ended", "no_active_run", "queue_full"}) {
		const std::string problem = frameProblem(makeCommandEventFrame(run, 7, command, 0, reason), std::string("command event frame rejected for ") + reason);
		QVERIFY2(problem.empty(), problem.c_str());
	}
}

// The checker has to reject what the schema forbids, or the checks above prove nothing.
void TestLiveFrame::schemaRejectsFaults() {
	GuiSimulationSnapshot snapshot;
	snapshot.trains = {locatedTrain()};
	snapshot.signalStates = {{"1-B0", 75, false, 2, false}};
	const json good = makeStateFrame(sampleRun(), 1, snapshot, true);
	// The sample frame conforms.
	QCOMPARE(schemaProblem(good), std::string());

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
		QVERIFY2(!schemaProblem(broken).empty(), (std::string("the schema accepts ") + fault.name).c_str());
	}
	QVERIFY2(!schemaProblem(json::object()).empty(), "the schema accepts an empty object");
	json steps = makeEventFrame(sampleRun(), 1, LiveEvent::StepsFinished, 3);
	steps["status"] = "completed";
	QVERIFY2(!schemaProblem(steps).empty(), "the schema accepts a status on steps_finished");

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
		QVERIFY2(!schemaProblem(broken).empty(), (std::string("the schema accepts ") + fault.name).c_str());
	}
}

void TestLiveFrame::fixtureRun() {
	QVERIFY2(fixtureRan(), "the fixture did not run");
	const FixtureRun& run = m_run;
	// The run settings are those of the fixture.
	QCOMPARE_EQ(run.info.stepSeconds, 1.0);
	QCOMPARE(run.info.horizonSteps, 1500);
	QCOMPARE_EQ(run.info.baseTimeSeconds, 25200.0);
	QCOMPARE(run.info.sceneName, "Characterization Line");
	// The run gives one snapshot per step, each that of its step.
	QCOMPARE(static_cast<int>(run.snapshots.size()), run.info.horizonSteps);
	for (std::size_t i = 0; i < run.snapshots.size(); ++i)
		QCOMPARE(run.snapshots[i]->timestep, static_cast<int>(i));

	const RunFrames& frames = m_frames;
	std::string problem = frameProblem(frames.hello, "hello frame of the run");
	QVERIFY2(problem.empty(), problem.c_str());
	std::vector<const json*> inOrder = {&frames.hello};
	for (const json& state : frames.states) inOrder.push_back(&state);
	for (const json& event : frames.events) inOrder.push_back(&event);
	for (std::size_t i = 0; i < inOrder.size(); ++i)
		QVERIFY2(inOrder[i]->at("seq") == i && inOrder[i]->at("run_id") == "line-run" && inOrder[i]->at("v") == kLiveContractVersion,
			("frame " + std::to_string(i) + " does not carry the version, the run and the position it should").c_str());

	bool moving = false, sawFailure = false, sawUnentered = false, sawLevel = false;
	for (const json& state : frames.states) {
		problem = frameProblem(state, "state frame " + std::to_string(state.at("step").get<int>()));
		QVERIFY2(problem.empty(), problem.c_str());
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
	for (const json& event : frames.events) {
		problem = frameProblem(event, "event frame");
		QVERIFY2(problem.empty(), problem.c_str());
	}
	// The run shows a moving train, an unentered train, a failed signal and a level.
	QVERIFY(moving);
	QVERIFY(sawUnentered);
	QVERIFY(sawFailure);
	QVERIFY(sawLevel);
	// The signals are sent with the first frame and again only when they change.
	QVERIFY(frames.states.front().contains("signals"));
	QCOMPARE_GT(frames.statesWithSignals, std::size_t(1));
	QCOMPARE_LT(frames.statesWithSignals, frames.states.size());
	QVERIFY2(frames.states.back().at("step") == run.info.horizonSteps - 1, "the last state frame is not that of the last step");
	const json& failureStart = frames.states[400];
	const auto failed = [](const json& signal) { return signal.at("failed") == true; };
	QVERIFY2(failureStart.contains("signals") && std::any_of(failureStart.at("signals").begin(), failureStart.at("signals").end(), failed),
		"the frame of the first step of the failure does not report a failed signal");
}

void TestLiveFrame::fixtureRunMatchesGoldenFile_data() {
	QTest::addColumn<int>("step");
	QTest::newRow("hello.json") << kHelloFile;
	for (const int step : kGoldenSteps)
		QTest::newRow(("state-" + std::to_string(step) + ".json").c_str()) << step;
	QTest::newRow("events.json") << kEventsFile;
}

// In the update mode the frames replace the golden file instead.
void TestLiveFrame::fixtureRunMatchesGoldenFile() {
	QFETCH(int, step);
	QVERIFY2(fixtureRan() && !m_frames.states.empty(), "the fixture did not run or did not give one snapshot per step");
	const json frame = step == kHelloFile ? m_frames.hello : step == kEventsFile ? json(m_frames.events)
																				 : m_frames.states[static_cast<std::size_t>(step)];
	const std::string problem = goldenProblem(kExpectDir, QTest::currentDataTag(), frame);
	QVERIFY2(problem.empty(), problem.c_str());
}

QTEST_GUILESS_MAIN(TestLiveFrame)
#include "test_liveframe.moc"
