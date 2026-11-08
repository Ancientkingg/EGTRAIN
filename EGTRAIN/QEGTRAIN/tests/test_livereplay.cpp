// Replays the commands of a live client against the line fixture (tests/fixtures/scenes/line) in this
// process. The run has a LiveServer with commands, and the test is the client: it sends the requests
// of a script through a REQ socket from the thread of the run, just before the step that each names,
// and reads the stream through a SUB socket on another thread.
//
//   test_livereplay
//
// The scripts are JSON-lines files in tests/live/commands. Each line is {"step": N, "cmd": ..., "args": ...}:
// the request is sent after step N-1 has run, so the run takes it at the top of step N.
//
// A run that receives only pause, resume and set_delay_ms must give the results of a run that receives
// nothing: the same files, the same standard output and the same state at every step. A pause here is
// ended by the reader of the stream, which sends a resume once the pause is reported and the run has
// stood still for a while.

#include "app/DispatchController.h"
#include "io/LiveCommand.h"
#include "io/LiveFrame.h"
#include "io/LiveServer.h"
#include "simulation/InitialParameters.h"
#include "simulation/Infrastructure.h"
#include "simulation/RunControl.h"
#include "simulation/Simulation.h"
#ifdef signals
#undef signals
#endif

#include <QCoreApplication>
#include <QObject>
#include <QTemporaryDir>
#include <QTest>

#include <nlohmann/json.hpp>
#include <zmq.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {
using nlohmann::json;
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

// The reader checks from its own thread: it records a failure here, and cleanup() fails the slot that
// recorded one.
std::mutex recordedMutex;
std::vector<std::string> recordedFailures;

void recordFailure(const std::string& failure) {
	const std::lock_guard<std::mutex> lock(recordedMutex);
	recordedFailures.push_back(failure);
}

const std::string kFixtureDir = EGTRAIN_TEST_SOURCE_DIR "/fixtures/scenes/line";
const std::string kScriptDir = EGTRAIN_TEST_SOURCE_DIR "/live/commands";

// The fixture is one track of this length (tests/characterization).
constexpr double kFixtureLengthKm = 16.0;
// A state frame for every this many steps.
constexpr int kFrameEvery = 10;
// How long a paused run has to stand still before the reader resumes it, and how long it is watched.
constexpr int kSettleMs = 60;
constexpr int kStandStillMs = 150;
constexpr int kReceiveMs = 20;
constexpr int kRunFinishedTimeoutMs = 30000;
// Timing files differ between two runs of the same input.
const char* const kTimingFile = "Computing_Times.txt";

// --- ZeroMQ clients ---

std::string commandText(const std::string& token, const std::string& id, const std::string& name, const json& args = nullptr) {
	json request = {{"v", kLiveContractVersion}, {"id", id}, {"token", token}, {"cmd", name}};
	if (!args.is_null())
		request["args"] = args;
	return request.dump();
}

// A REQ socket for the command endpoint. One thread uses it.
class Requester {
public:
	Requester(zmq::context_t& context, const std::string& endpoint, std::string token)
		: m_socket(context, zmq::socket_type::req), m_token(std::move(token)) {
		m_socket.set(zmq::sockopt::linger, 0);
		m_socket.set(zmq::sockopt::rcvtimeo, 5000);
		m_socket.connect(endpoint);
	}

	// The parsed reply, or null when none came.
	json ask(const std::string& id, const std::string& name, const json& args = nullptr) {
		const std::string text = commandText(m_token, id, name, args);
		if (!m_socket.send(zmq::buffer(text), zmq::send_flags::none))
			return nullptr;
		zmq::message_t reply;
		if (!m_socket.recv(reply, zmq::recv_flags::none))
			return nullptr;
		return json::parse(reply.to_string(), nullptr, false);
	}

private:
	zmq::socket_t m_socket;
	std::string m_token;
};

struct Endpoints {
	std::string state;
	std::string command;
	std::string token;
};

Endpoints readEndpoints(const fs::path& directory) {
	std::ifstream in(directory / "live-endpoint.json", std::ios::binary);
	const json file = json::parse(in, nullptr, false);
	Endpoints endpoints;
	if (file.is_object()) {
		endpoints.state = file.value("state", "");
		endpoints.command = file.value("command", "");
		endpoints.token = file.value("token", "");
	}
	return endpoints;
}

struct Frame {
	std::string topic;
	json body;
};

// Reads the stream on its own thread and keeps every frame but the state frames, of which it keeps
// the steps. When the run is paused it checks that no state frame comes, then resumes the run.
class Reader {
public:
	Reader(zmq::context_t& context, const Endpoints& endpoints)
		: m_subscriber(context, zmq::socket_type::sub), m_requester(context, endpoints.command, endpoints.token) {
		m_subscriber.set(zmq::sockopt::linger, 0);
		m_subscriber.set(zmq::sockopt::subscribe, "");
		m_subscriber.set(zmq::sockopt::rcvtimeo, kReceiveMs);
		m_subscriber.connect(endpoints.state);
		m_thread = std::thread([this] { loop(); });
	}

	~Reader() {
		m_stop = true;
		m_thread.join();
	}

	bool sawHello() const { return m_sawHello; }

	// True once the event that ends the run has come.
	bool waitForRunFinished(int timeoutMs) const {
		const Clock::time_point deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
		while (!m_finished && Clock::now() < deadline)
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		return m_finished;
	}

	std::vector<Frame> frames() const {
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_frames;
	}

	// The states the run showed while the reader had reported it paused.
	int statesWhilePaused() const { return m_statesWhilePaused; }
	int pausesSeen() const { return m_pausesSeen; }

	// Forgets what has been read of a run, to read the next. The reader stays subscribed.
	void forget() {
		std::lock_guard<std::mutex> lock(m_mutex);
		m_frames.clear();
		m_states = 0;
		m_finished = false;
		m_statesWhilePaused = 0;
		m_pausesSeen = 0;
	}

private:
	void store(const std::string& topic, json body) {
		std::lock_guard<std::mutex> lock(m_mutex);
		if (topic == "state") {
			++m_states;
			return;
		}
		if (topic == "hello")
			m_sawHello = true;
		if (topic == "event" && body.value("kind", "") == "applied" && body.value("cmd", "") == "pause")
			++m_pausesToEnd;
		if (topic == "event" && body.value("kind", "") == "run_finished")
			m_finished = true;
		m_frames.push_back({topic, std::move(body)});
	}

	bool receiveOne() {
		zmq::message_t topic;
		if (!m_subscriber.recv(topic, zmq::recv_flags::none))
			return false;
		zmq::message_t text;
		if (!topic.more() || !m_subscriber.recv(text, zmq::recv_flags::none))
			return false;
		store(topic.to_string(), json::parse(text.to_string(), nullptr, false));
		return true;
	}

	void receiveFor(int milliseconds) {
		const Clock::time_point end = Clock::now() + std::chrono::milliseconds(milliseconds);
		while (Clock::now() < end)
			receiveOne();
	}

	int states() const {
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_states;
	}

	void endPause() {
		++m_pausesSeen;
		receiveFor(kSettleMs);
		const int before = states();
		receiveFor(kStandStillMs);
		m_statesWhilePaused += states() - before;
		const json reply = m_requester.ask("reader-resume-" + std::to_string(m_pausesSeen), "resume");
		if (!reply.is_object() || reply.value("status", "") != "accepted")
			recordFailure("the reader's resume is accepted: " + reply.dump());
	}

	void loop() {
		while (!m_stop) {
			receiveOne();
			int pauses;
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				pauses = m_pausesToEnd;
				m_pausesToEnd = 0;
			}
			for (int i = 0; i < pauses; ++i)
				endPause();
		}
	}

	zmq::socket_t m_subscriber;
	Requester m_requester;
	std::thread m_thread;
	std::atomic<bool> m_stop{false};
	std::atomic<bool> m_sawHello{false};
	std::atomic<bool> m_finished{false};
	std::atomic<int> m_statesWhilePaused{0};
	std::atomic<int> m_pausesSeen{0};
	mutable std::mutex m_mutex;
	// Guarded by m_mutex.
	std::vector<Frame> m_frames;
	int m_states = 0;
	int m_pausesToEnd = 0;
};

// The live stream of the process and the client that reads it: the server with commands, a reader on
// its own thread and a requester for the thread of the run. It is made before the first run it serves
// and may serve several.
class Stream {
public:
	Stream() {
		if (!m_directory.isValid()) {
			m_problem = "no temporary folder for the stream";
			return;
		}
		const fs::path live = fs::path(m_directory.path().toStdString()) / "live";
		std::string error;
		if (!simulation.startLive(0, kFrameEvery, true, live, &error)) {
			m_problem = "the live stream does not start: " + error;
			return;
		}
		m_started = true;
		const Endpoints endpoints = readEndpoints(live);
		m_reader = std::make_unique<Reader>(m_context, endpoints);
		m_requester = std::make_unique<Requester>(m_context, endpoints.command, endpoints.token);
	}

	~Stream() {
		m_reader.reset();
		if (m_started)
			simulation.stopLive();
	}

	Stream(const Stream&) = delete;
	Stream& operator=(const Stream&) = delete;

	bool ok() const { return m_started; }
	// Why the stream did not start, or an empty text.
	const std::string& problem() const { return m_problem; }
	Reader& reader() { return *m_reader; }
	Requester& requester() { return *m_requester; }

private:
	QTemporaryDir m_directory;
	// The sockets are closed before the context.
	zmq::context_t m_context;
	std::unique_ptr<Reader> m_reader;
	std::unique_ptr<Requester> m_requester;
	bool m_started = false;
	std::string m_problem;
};

// --- The script ---

struct Line {
	int step = 0;
	std::string name;
	json args = nullptr;
};

// The lines of a script. The first fault of the file goes to `problem`.
std::vector<Line> readScript(const std::string& file, std::string& problem) {
	std::vector<Line> lines;
	std::ifstream in(file);
	std::string text;
	while (std::getline(in, text)) {
		if (text.empty())
			continue;
		const json parsed = json::parse(text, nullptr, false);
		if (!parsed.is_object() || !parsed.contains("step") || !parsed.contains("cmd")) {
			if (problem.empty())
				problem = "a line of " + file + " is no command: " + text;
			continue;
		}
		Line line;
		line.step = parsed.at("step").get<int>();
		line.name = parsed.at("cmd").get<std::string>();
		if (parsed.contains("args"))
			line.args = parsed.at("args");
		lines.push_back(std::move(line));
	}
	if (lines.empty() && problem.empty())
		problem = "the script " + file + " has no line";
	return lines;
}

// --- The run ---

class CoutCapture {
public:
	CoutCapture() : m_saved(std::cout.rdbuf(m_text.rdbuf())) {}
	~CoutCapture() { std::cout.rdbuf(m_saved); }
	std::string text() const { return m_text.str(); }

private:
	std::ostringstream m_text;
	std::streambuf* m_saved;
};

// What a run leaves that must not depend on the commands it received.
struct Results {
	// The first fault of the run or of its requests, empty when there was none.
	std::string problem;
	bool ran = false;
	int horizon = 0;
	std::map<std::string, std::string> files;
	std::string standardOutput;
	// The state frame of each step, with the run id and seq left out.
	std::vector<std::string> states;
	bool completed = false;
	std::vector<Frame> frames;
	int statesWhilePaused = 0;
	int pausesSeen = 0;
	bool streamFinished = false;
};

std::map<std::string, std::string> filesOf(const fs::path& directory) {
	std::map<std::string, std::string> files;
	for (const fs::directory_entry& entry : fs::recursive_directory_iterator(directory)) {
		if (!entry.is_regular_file() || entry.path().filename() == kTimingFile)
			continue;
		std::ifstream in(entry.path(), std::ios::binary);
		files[fs::relative(entry.path(), directory).generic_string()] = std::string(std::istreambuf_iterator<char>(in), {});
	}
	return files;
}

// What a run does besides the script: called with the number of the step that has just run.
using StepHook = std::function<void(int step)>;

struct RunOptions {
	// Requests that are sent after step N-1, so that the run takes them at the top of step N.
	std::vector<Line> script;
	// Called after each step, before the script.
	StepHook onStep;
	// Requests that are sent before the run starts.
	std::vector<Line> before;
};

// Runs two trains of the fixture through a signal failure, as test_liveframe does. With a stream the run
// publishes to it and the script is sent to its server.
Results runFixture(const std::string& fixture, const RunOptions& options, Stream* stream = nullptr) {
	Results results;
	const auto fail = [&results](const std::string& problem) {
		if (results.problem.empty())
			results.problem = problem;
	};
	SceneLoadResult loaded = loadScene(fixture);
	if (hasErrors(loaded.diagnostics)) {
		fail("cannot load " + fixture);
		return results;
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

	QTemporaryDir work;
	if (!work.isValid()) {
		fail("no temporary folder");
		return results;
	}
	const fs::path output = fs::path(work.path().toStdString()) / "output";
	fs::create_directories(output);
	initial_variables.GUI = 0;
	initial_variables.PAX_GUI = false;
	initial_variables.exportDetailedTrajectories = false;
	initial_variables.OutputMainFolder = output.string();
	initial_variables.InputMainFolder.clear();

	CoutCapture standardOutput;
	if (hasErrors(simulation.prepareScene(loaded.scene, "signal-failure-forward", selection))) {
		fail("cannot prepare " + fixture);
		return results;
	}

	LiveRunInfo info;
	info.runId = "replay";
	info.sceneName = loaded.scene.name;
	info.baseTimeSeconds = initial_variables.startingSimulationTime;
	info.horizonSteps = static_cast<int>(initial_variables.times);
	info.stepSeconds = timestep;
	results.horizon = info.horizonSteps;

	int sent = 0;
	const auto send = [&](const Line& line) {
		const json reply = stream->requester().ask("s" + std::to_string(sent++), line.name, line.args);
		if (!reply.is_object() || reply.value("status", "") != "accepted")
			fail("the server accepts " + line.name + ": " + reply.dump());
	};
	for (const Line& line : options.before)
		send(line);

	QObject receiver;
	const auto onSnapshot = [&] {
		const auto snapshot = simulation.takeSimulationSnapshot();
		if (!snapshot)
			return;
		results.states.push_back(makeStateFrame(info, 0, *snapshot, true).dump());
		if (!stream)
			return;
		const int step = snapshot->timestep;
		if (step == 0) {
			// The subscription is certain once a hello has come back.
			for (int attempt = 0; attempt < 500 && !stream->reader().sawHello(); ++attempt) {
				stream->requester().ask("describe", "describe");
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
			}
			if (!stream->reader().sawHello())
				fail("the reader gets the hello");
		}
		if (options.onStep)
			options.onStep(step);
		for (const Line& line : options.script)
			if (line.step == step + 1)
				send(line);
	};
	QObject::connect(&simulation, &DispatchController::snapshotAvailable, &receiver, onSnapshot, Qt::DirectConnection);
	simulation.runSimulation();
	QObject::disconnect(&simulation, &DispatchController::snapshotAvailable, &receiver, nullptr);
	results.ran = true;
	results.completed = simulation.lastRunCompleted();
	if (stream) {
		results.streamFinished = stream->reader().waitForRunFinished(kRunFinishedTimeoutMs);
		results.frames = stream->reader().frames();
		results.statesWhilePaused = stream->reader().statesWhilePaused();
		results.pausesSeen = stream->reader().pausesSeen();
	}
	std::cout.flush();
	results.standardOutput = standardOutput.text();
	results.files = filesOf(output);
	return results;
}

// A run that publishes to a stream of its own.
Results runStreamed(const std::string& fixture, const RunOptions& options) {
	Stream stream;
	if (stream.ok())
		return runFixture(fixture, options, &stream);
	Results results;
	results.problem = stream.problem();
	return results;
}

// --- Checks ---

// The first way in which the results of a run differ from the expected ones, or an empty text.
std::string sameResultsProblem(const Results& expected, const Results& actual, const std::string& what) {
	if (!actual.ran || !expected.ran)
		return what + ": both runs ran";
	if (actual.standardOutput != expected.standardOutput)
		return what + ": the standard output differs";
	if (actual.states.size() != expected.states.size())
		return what + ": the number of steps differs";
	for (std::size_t i = 0; i < actual.states.size() && i < expected.states.size(); ++i)
		if (actual.states[i] != expected.states[i])
			return what + ": the state of step " + std::to_string(i) + " differs";
	for (const auto& file : expected.files) {
		const auto found = actual.files.find(file.first);
		if (found == actual.files.end())
			return what + ": " + file.first + " is missing";
		if (found->second != file.second)
			return what + ": " + file.first + " differs";
	}
	for (const auto& file : actual.files)
		if (expected.files.count(file.first) != 1)
			return what + ": " + file.first + " is extra";
	if (expected.files.empty())
		return what + ": the run left no file";
	return {};
}

// The events that report commands, in the order of the stream: kind, cmd, step, reason.
std::vector<std::string> commandEvents(const std::vector<Frame>& frames) {
	std::vector<std::string> events;
	for (const Frame& frame : frames) {
		const std::string kind = frame.body.value("kind", "");
		if (frame.topic != "event" || (kind != "applied" && kind != "rejected"))
			continue;
		events.push_back(kind + " " + frame.body.value("cmd", "") + " " + std::to_string(frame.body.value("step", -1))
			+ (frame.body.contains("reason") ? " " + frame.body.at("reason").get<std::string>() : ""));
	}
	return events;
}

std::string listOf(const std::vector<std::string>& values) {
	std::string text;
	for (const std::string& value : values)
		text += "[" + value + "]";
	return text;
}

// The stream of a run is one hello, then frames in the order of seq, with the same run id; the events
// of the end are the last two. Returns the first fault, or an empty text.
std::string streamProblem(const Results& run, const std::string& what, const std::string& status, int stepsFinished) {
	if (!run.streamFinished)
		return what + ": the stream reaches run_finished";
	std::string runId;
	std::int64_t lastSeq = -1;
	int hellos = 0;
	for (const Frame& frame : run.frames) {
		if (!frame.body.is_object())
			return what + ": a frame is no JSON object";
		hellos += frame.topic == "hello" ? 1 : 0;
		if (runId.empty())
			runId = frame.body.value("run_id", "");
		if (frame.body.value("run_id", "") != runId)
			return what + ": the run id changes";
		if (frame.topic == "hello" && lastSeq >= 0)
			continue;
		const std::int64_t seq = frame.body.value("seq", std::int64_t(-1));
		if (seq <= lastSeq)
			return what + ": seq " + std::to_string(seq) + " does not follow " + std::to_string(lastSeq);
		lastSeq = seq;
	}
	if (hellos < 1)
		return what + ": the reader got the hello";
	const std::vector<Frame>& frames = run.frames;
	if (frames.size() < 2)
		return {};
	const json& finished = frames[frames.size() - 2].body;
	const json& end = frames.back().body;
	if (finished.value("kind", "") != "steps_finished" || finished.value("step", -1) != stepsFinished)
		return what + ": steps_finished says " + finished.dump();
	if (end.value("kind", "") != "run_finished" || end.value("status", "") != status || end.value("step", -1) != stepsFinished)
		return what + ": run_finished says " + end.dump();
	return {};
}

// The command events of the run when they differ from `expected`, or an empty text.
std::string eventsProblem(const Results& run, const std::vector<std::string>& expected, const std::string& what) {
	const std::vector<std::string> actual = commandEvents(run.frames);
	if (actual == expected)
		return {};
	return what + ": the command events are " + listOf(actual) + " and not " + listOf(expected);
}
} // namespace


class TestLiveReplay : public QObject {
	Q_OBJECT

private slots:
	void cleanup();
	void fetchedCommandIsAppliedInSameStep();
	void fetchedResumeEndsPause();
	void fetchedStopEndsRun();
	void stepWithoutHookGoesOn();
	void referenceRun();
	void secondRunWithoutCommands();
	void pauseResumeAndDelay();
	void stopEndsRun();
	void commandsBeforeRun();
	void fullControl();

private:
	const Results& reference();

	std::optional<Results> m_reference;
};

void TestLiveReplay::cleanup() {
	std::vector<std::string> failures;
	{
		const std::lock_guard<std::mutex> lock(recordedMutex);
		failures.swap(recordedFailures);
	}
	std::string text;
	for (const std::string& failure : failures)
		text += (text.empty() ? "" : "; ") + failure;
	QVERIFY2(failures.empty(), text.c_str());
}

// The run without commands that the others are compared with. It runs when a slot first needs it.
const Results& TestLiveReplay::reference() {
	if (!m_reference)
		m_reference = runFixture(kFixtureDir, RunOptions());
	return *m_reference;
}

// The hook that lets the owner of a run submit commands from another source, also while it waits.
void TestLiveReplay::fetchedCommandIsAppliedInSameStep() {
	RunControl control;
	int calls = 0;
	control.setDelayMs(0);
	const bool proceed = control.beginStep([&] {
		++calls;
		control.submit({RunCommandKind::SetDelayMs, 7});
	});
	// fetch: a command fetched at the top is applied in the same step.
	QVERIFY(proceed);
	QCOMPARE(calls, 1);
	QCOMPARE(control.delayMs(), 7);
}

void TestLiveReplay::fetchedResumeEndsPause() {
	RunControl control;
	control.requestPause();
	int calls = 0;
	const bool proceed = control.beginStep([&] {
		// The first call is at the top of the step, before the pause is looked at.
		if (++calls == 3)
			control.submit({RunCommandKind::Resume});
	});
	// fetch: a resume that is fetched during a pause ends it.
	QVERIFY(proceed);
	QCOMPARE(calls, 3);
	QVERIFY(!control.isPauseRequested());
}

void TestLiveReplay::fetchedStopEndsRun() {
	RunControl control;
	control.requestPause();
	int calls = 0;
	const bool proceed = control.beginStep([&] {
		if (++calls == 2)
			control.submit({RunCommandKind::Stop});
	});
	// fetch: a stop that is fetched during a pause ends the run.
	QVERIFY(!proceed);
	QVERIFY(control.isStopRequested());
	QVERIFY(control.wasCancellationRequested());
}

void TestLiveReplay::stepWithoutHookGoesOn() {
	RunControl control;
	// fetch: a step without a hook goes on.
	QVERIFY(control.beginStep());
	QVERIFY(control.beginStep({}));
}

void TestLiveReplay::referenceRun() {
	const Results& run = reference();
	QVERIFY2(run.problem.empty(), run.problem.c_str());
	// The reference run completes its 1500 steps.
	QCOMPARE(run.horizon, 1500);
	QVERIFY(run.completed);
	// The reference run gives a snapshot for every step.
	QCOMPARE(static_cast<int>(run.states.size()), run.horizon);
}

// Two runs of the same input in one process are equal, so a difference in the slots below comes from the commands.
void TestLiveReplay::secondRunWithoutCommands() {
	const Results& expected = reference();
	QVERIFY2(expected.problem.empty(), expected.problem.c_str());
	const Results run = runFixture(kFixtureDir, RunOptions());
	QVERIFY2(run.problem.empty(), run.problem.c_str());
	const std::string problem = sameResultsProblem(expected, run, "a second run without commands");
	QVERIFY2(problem.empty(), problem.c_str());
}

// pause, resume and set_delay_ms, replayed from the script.
void TestLiveReplay::pauseResumeAndDelay() {
	const Results& expected = reference();
	QVERIFY2(expected.problem.empty(), expected.problem.c_str());
	RunOptions options;
	std::string problem;
	options.script = readScript(kScriptDir + "/pause-resume-delay.jsonl", problem);
	QVERIFY2(problem.empty(), problem.c_str());
	const Results run = runStreamed(kFixtureDir, options);
	QVERIFY2(run.problem.empty(), run.problem.c_str());
	problem = sameResultsProblem(expected, run, "a run with pause, resume and set_delay_ms");
	QVERIFY2(problem.empty(), problem.c_str());
	QVERIFY2(run.completed, "the run with commands completes");
	problem = streamProblem(run, "pause, resume and set_delay_ms", "completed", run.horizon);
	QVERIFY2(problem.empty(), problem.c_str());
	problem = eventsProblem(run,
		{"applied set_delay_ms 10", "applied pause 40", "applied resume 40", "applied resume 60", "applied set_delay_ms 200",
			"applied pause 300", "applied set_delay_ms 300", "applied resume 300", "applied set_delay_ms 301",
			"rejected pause 1500 run_ended"},
		"pause, resume and set_delay_ms");
	QVERIFY2(problem.empty(), problem.c_str());
	// The run paused twice.
	QCOMPARE(run.pausesSeen, 2);
	// No state came while the run was paused.
	QCOMPARE(run.statesWhilePaused, 0);
}

// stop: the run ends as cancelled with the steps that ran.
void TestLiveReplay::stopEndsRun() {
	RunOptions options;
	std::string problem;
	options.script = readScript(kScriptDir + "/stop.jsonl", problem);
	QVERIFY2(problem.empty(), problem.c_str());
	const Results run = runStreamed(kFixtureDir, options);
	QVERIFY2(run.problem.empty(), run.problem.c_str());
	// A stopped run does not complete.
	QVERIFY(run.ran);
	QVERIFY(!run.completed);
	// A stop before step 100 leaves 100 steps.
	QCOMPARE(run.states.size(), std::size_t(100));
	problem = streamProblem(run, "stop", "cancelled", 100);
	QVERIFY2(problem.empty(), problem.c_str());
	problem = eventsProblem(run, {"applied stop 100"}, "stop");
	QVERIFY2(problem.empty(), problem.c_str());
}

// A command that arrives while no run goes on is turned down when the next run begins, and does not
// reach that run. The first run leaves the reader subscribed, so that it sees the events of the second.
void TestLiveReplay::commandsBeforeRun() {
	const Results& expected = reference();
	QVERIFY2(expected.problem.empty(), expected.problem.c_str());
	Stream stream;
	QVERIFY2(stream.ok(), stream.problem().c_str());
	const Results first = runFixture(kFixtureDir, RunOptions(), &stream);
	QVERIFY2(first.problem.empty(), first.problem.c_str());
	QVERIFY2(first.streamFinished, "the first run of the stream ends");
	stream.reader().forget();
	RunOptions options;
	options.before = {{0, "pause", nullptr}, {0, "stop", nullptr}};
	const Results run = runFixture(kFixtureDir, options, &stream);
	QVERIFY2(run.problem.empty(), run.problem.c_str());
	std::string problem = sameResultsProblem(expected, run, "a run after commands that came before it");
	QVERIFY2(problem.empty(), problem.c_str());
	problem = streamProblem(run, "commands before the run", "completed", run.horizon);
	QVERIFY2(problem.empty(), problem.c_str());
	problem = eventsProblem(run, {"rejected pause 0 no_active_run", "rejected stop 0 no_active_run"}, "commands before the run");
	QVERIFY2(problem.empty(), problem.c_str());
	// The run was not paused by a command that came before it.
	QCOMPARE(run.pausesSeen, 0);
}

// A command is turned down when the control of the run already holds as many commands as it takes.
void TestLiveReplay::fullControl() {
	const Results& expected = reference();
	QVERIFY2(expected.problem.empty(), expected.problem.c_str());
	RunOptions options;
	options.script = {{51, "set_delay_ms", {{"delay_ms", 0}}}, {53, "set_delay_ms", {{"delay_ms", 0}}}};
	options.onStep = [](int step) {
		if (step != 50)
			return;
		const std::shared_ptr<RunControl> control = simulation.runControl();
		for (std::size_t i = 0; i < RunControl::kCommandQueueSize; ++i)
			control->submit({RunCommandKind::SetDelayMs, 0});
	};
	const Results run = runStreamed(kFixtureDir, options);
	QVERIFY2(run.problem.empty(), run.problem.c_str());
	std::string problem = sameResultsProblem(expected, run, "a run with a full control");
	QVERIFY2(problem.empty(), problem.c_str());
	problem = eventsProblem(run, {"rejected set_delay_ms 51 queue_full", "applied set_delay_ms 53"}, "a full control");
	QVERIFY2(problem.empty(), problem.c_str());
}

QTEST_GUILESS_MAIN(TestLiveReplay)
#include "test_livereplay.moc"
