// A small client of the live stream (docs/architecture/external-sharing.md).
//
//   live_client ENDPOINT_FILE [--runs N] [--max-states N] [--timeout SECONDS]
//
// ENDPOINT_FILE is the live-endpoint.json that QEGTRAIN writes to its output folder when it starts with
// --live. The client subscribes to the state endpoint of the file and reads frames until N runs have
// finished (default 1; 0 stops at the first hello), until it has read N state frames, or until the
// timeout (default 60 seconds) has passed.
//
// A subscriber that joins after a run has begun misses the hello frame. When the file also names a
// command endpoint (QEGTRAIN started with --live-commands), the client sends "describe" until the
// server publishes the hello again, so it can start at any time.
//
// For every run that it sees the client checks what the contract promises: the version, a constant
// run id, a seq that increases, steps that increase, the shape of each train, and a time axis that
// agrees with the hello frame. It writes one JSON line per run to standard output:
//
//   run_id, scene_name, joined_late (the hello was not seen), base_time_s, trains, services (the
//   [service_id, occurrence] of each train of the hello), horizon_steps, states, first_step, last_step,
//   missed (frames missing in seq), positions (train entries with a position), moving (train entries
//   with a speed above 0), state_bytes, signal_frames (state frames with the signal list),
//   steps_finished (the step of that event, or null), status (of run_finished, or null)
//
// Each broken promise is written to standard error as "violation: ...". The exit status is 0 when the
// runs were read and nothing was broken, 1 after a violation or a timeout, and 2 for a bad call.

#include <nlohmann/json.hpp>
#include <zmq.hpp>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {
using nlohmann::json;
using Clock = std::chrono::steady_clock;

constexpr int kContractVersion = 1;
constexpr int kDefaultTimeoutSeconds = 60;
constexpr int kDescribeIntervalMs = 50;
constexpr int kReceiveTimeoutMs = 20;

struct Endpoints {
	std::string state;
	std::string command;
	std::string token;
};

// The endpoints of the file, or nothing while the file is missing or incomplete.
std::optional<Endpoints> readEndpoints(const std::string& path) {
	std::ifstream in(path, std::ios::binary);
	const json file = json::parse(in, nullptr, false);
	if (!file.is_object() || !file.contains("state") || !file.at("state").is_string())
		return std::nullopt;
	Endpoints endpoints;
	endpoints.state = file.at("state").get<std::string>();
	if (file.contains("command") && file.at("command").is_string() && file.contains("token") && file.at("token").is_string()) {
		endpoints.command = file.at("command").get<std::string>();
		endpoints.token = file.at("token").get<std::string>();
	}
	return endpoints;
}

struct Run {
	std::string runId;
	bool sawHello = false;
	std::string sceneName;
	std::size_t trains = 0;
	json services = json::array();
	std::int64_t horizonSteps = 0;
	double baseTimeSeconds = 0.0;
	double stepSeconds = 1.0;
	std::int64_t lastSeq = -1;
	// A state or event frame has been read, so a jump in seq is a loss and not the start of the client.
	bool sawFrame = false;
	std::int64_t missed = 0;
	int states = 0;
	std::int64_t firstStep = -1;
	std::int64_t lastStep = -1;
	std::int64_t positions = 0;
	std::int64_t moving = 0;
	std::int64_t stateBytes = 0;
	int signalFrames = 0;
	std::optional<std::int64_t> stepsFinished;
	std::optional<std::string> status;
};

bool isNumberOrNull(const json& value) {
	return value.is_null() || value.is_number();
}

// Reads the frames of a stream and keeps what the contract lets a client check.
class Reader {
public:
	const std::vector<Run>& runs() const { return m_runs; }
	const std::vector<std::string>& violations() const { return m_violations; }
	int finishedRuns() const { return m_finished; }
	int statesRead() const { return m_statesRead; }
	bool sawHello() const { return !m_runs.empty() && m_runs.back().sawHello; }

	void handle(const std::string& topic, const std::string& text) {
		const json frame = json::parse(text, nullptr, false);
		if (!frame.is_object() || !frame.contains("run_id") || !frame.at("run_id").is_string() || !frame.contains("seq")
			|| !frame.at("seq").is_number_integer()) {
			violation("a " + topic + " frame has no run_id or seq");
			return;
		}
		if (!frame.contains("v") || frame.at("v") != kContractVersion)
			violation("a " + topic + " frame does not have version " + std::to_string(kContractVersion));
		const std::string runId = frame.at("run_id").get<std::string>();
		const std::int64_t seq = frame.at("seq").get<std::int64_t>();
		if (topic == "hello") {
			if (m_runs.empty() || m_runs.back().runId != runId) {
				if (seq != 0)
					violation("run " + runId + ": the hello frame has seq " + std::to_string(seq) + " and not 0");
				m_runs.emplace_back();
				m_runs.back().runId = runId;
				m_runs.back().lastSeq = seq;
			} else if (m_runs.back().sawHello) {
				return; // the server publishes the hello again when a client asks to describe the run
			}
			readHello(m_runs.back(), frame);
			return;
		}
		if (m_runs.empty() || m_runs.back().runId != runId) {
			m_runs.emplace_back();
			m_runs.back().runId = runId;
		}
		Run& run = m_runs.back();
		if (run.lastSeq >= 0 && seq <= run.lastSeq)
			violation("run " + runId + ": seq " + std::to_string(seq) + " does not follow " + std::to_string(run.lastSeq));
		else if (run.sawFrame)
			run.missed += seq - run.lastSeq - 1;
		run.lastSeq = seq;
		run.sawFrame = true;
		if (topic == "state")
			handleState(run, frame, text.size());
		else if (topic == "event")
			handleEvent(run, frame);
		else
			violation("a frame has the topic " + topic);
	}

private:
	void violation(const std::string& what) { m_violations.push_back(what); }

	void readHello(Run& run, const json& frame) {
		run.sawHello = true;
		const json units = {{"distance", "m"}, {"time", "s"}, {"speed", "m/s"}, {"x", "km"}};
		if (!frame.contains("units") || frame.at("units") != units)
			violation("run " + run.runId + ": the units of the hello frame are not " + units.dump());
		if (!frame.contains("trains") || !frame.at("trains").is_array() || !frame.contains("scene_name") || !frame.contains("base_time_s")
			|| !frame.contains("horizon_steps") || !frame.contains("step_s") || !frame.at("base_time_s").is_number()
			|| !frame.at("horizon_steps").is_number_integer() || !frame.at("step_s").is_number()) {
			violation("run " + run.runId + ": the hello frame lacks a member");
			return;
		}
		run.trains = frame.at("trains").size();
		for (const json& train : frame.at("trains")) {
			if (!train.is_object() || !train.contains("service_id") || !train.at("service_id").is_string() || !train.contains("occurrence")
				|| !train.at("occurrence").is_number_integer() || train.at("occurrence").get<std::int64_t>() < 1) {
				violation("run " + run.runId + ": a train of the hello frame has no service_id or no occurrence counted from 1");
				return;
			}
			run.services.push_back(json::array({train.at("service_id"), train.at("occurrence")}));
		}
		run.sceneName = frame.at("scene_name").get<std::string>();
		run.horizonSteps = frame.at("horizon_steps").get<std::int64_t>();
		run.baseTimeSeconds = frame.at("base_time_s").get<double>();
		run.stepSeconds = frame.at("step_s").get<double>();
	}

	void handleState(Run& run, const json& frame, std::size_t bytes) {
		const std::string where = "run " + run.runId + ": ";
		if (!frame.contains("step") || !frame.at("step").is_number_integer() || !frame.contains("sim_time_s") || !frame.contains("clock_s")
			|| !frame.at("sim_time_s").is_number() || !frame.at("clock_s").is_number() || !frame.contains("trains")
			|| !frame.at("trains").is_array()) {
			violation(where + "a state frame lacks a member");
			return;
		}
		const std::int64_t step = frame.at("step").get<std::int64_t>();
		if (run.lastStep >= 0 && step <= run.lastStep)
			violation(where + "step " + std::to_string(step) + " does not follow " + std::to_string(run.lastStep));
		if (run.stepsFinished)
			violation(where + "a state frame for step " + std::to_string(step) + " follows steps_finished");
		if (run.sawHello) {
			const double simulationTime = frame.at("sim_time_s").get<double>();
			if (std::fabs(simulationTime - static_cast<double>(step) * run.stepSeconds) > 1e-6
				|| std::fabs(frame.at("clock_s").get<double>() - run.baseTimeSeconds - simulationTime) > 1e-6)
				violation(where + "the time of step " + std::to_string(step) + " does not follow from the hello frame");
			if (frame.at("trains").size() != run.trains)
				violation(where + "step " + std::to_string(step) + " has another number of trains than the hello frame");
		}
		for (const json& train : frame.at("trains"))
			checkTrain(run, step, train);
		if (frame.contains("signals")) {
			if (!frame.at("signals").is_array())
				violation(where + "the signals of step " + std::to_string(step) + " are no array");
			++run.signalFrames;
		}
		if (run.firstStep < 0)
			run.firstStep = step;
		run.lastStep = step;
		run.stateBytes += static_cast<std::int64_t>(bytes);
		++run.states;
		++m_statesRead;
	}

	void checkTrain(Run& run, std::int64_t step, const json& train) {
		const std::string where = "run " + run.runId + ": step " + std::to_string(step) + ": ";
		static const char* const kMembers[] = {"service_id", "occurrence", "operating_code", "in_network", "direction",
			"route_pos_m", "x_km", "speed_ms", "onboard"};
		for (const char* member : kMembers) {
			if (!train.is_object() || !train.contains(member)) {
				violation(where + "a train lacks " + member);
				return;
			}
		}
		if (!isNumberOrNull(train.at("route_pos_m")) || !isNumberOrNull(train.at("x_km")) || !isNumberOrNull(train.at("speed_ms")))
			violation(where + "a position or speed is neither a number nor null");
		if (train.at("direction") != "forward" && train.at("direction") != "reversed")
			violation(where + "a direction is neither forward nor reversed");
		if (train.at("route_pos_m").is_number())
			++run.positions;
		if (train.at("speed_ms").is_number() && train.at("speed_ms").get<double>() < 0.0)
			violation(where + "a speed is negative");
		if (train.at("speed_ms").is_number() && train.at("speed_ms").get<double>() > 0.0)
			++run.moving;
	}

	void handleEvent(Run& run, const json& frame) {
		const std::string where = "run " + run.runId + ": ";
		if (!frame.contains("kind") || !frame.at("kind").is_string() || !frame.contains("step") || !frame.at("step").is_number_integer()) {
			violation(where + "an event frame lacks kind or step");
			return;
		}
		const std::string kind = frame.at("kind").get<std::string>();
		const std::int64_t step = frame.at("step").get<std::int64_t>();
		if (kind == "steps_finished") {
			if (run.lastStep >= step)
				violation(where + "steps_finished says " + std::to_string(step) + " steps, after the state of step " + std::to_string(run.lastStep));
			run.stepsFinished = step;
		} else if (kind == "run_finished") {
			if (!frame.contains("status") || !frame.at("status").is_string()) {
				violation(where + "run_finished has no status");
				return;
			}
			run.status = frame.at("status").get<std::string>();
			++m_finished;
		} else {
			violation(where + "an event of kind " + kind);
		}
	}

	std::vector<Run> m_runs;
	std::vector<std::string> m_violations;
	int m_finished = 0;
	int m_statesRead = 0;
};

// Asks the server to publish the hello of the run that goes on. Returns the reason when the server
// refuses and "" when it accepts or does not answer.
std::string describe(zmq::context_t& context, const Endpoints& endpoints) {
	zmq::socket_t request(context, zmq::socket_type::req);
	request.set(zmq::sockopt::linger, 0);
	request.set(zmq::sockopt::rcvtimeo, 500);
	request.connect(endpoints.command);
	const json message = {{"v", kContractVersion}, {"id", "describe"}, {"token", endpoints.token}, {"cmd", "describe"}};
	const std::string text = message.dump();
	if (!request.send(zmq::buffer(text), zmq::send_flags::none))
		return "";
	zmq::message_t reply;
	if (!request.recv(reply, zmq::recv_flags::none))
		return "";
	const json answer = json::parse(reply.to_string(), nullptr, false);
	if (!answer.is_object() || answer.value("status", "") == "accepted")
		return "";
	return answer.value("reason", "rejected");
}

// A frame is two parts: the topic and the JSON text.
bool receiveFrame(zmq::socket_t& socket, std::string& topic, std::string& text) {
	zmq::message_t first;
	if (!socket.recv(first, zmq::recv_flags::none))
		return false;
	zmq::message_t second;
	if (!first.more() || !socket.recv(second, zmq::recv_flags::none))
		return false;
	topic = first.to_string();
	text = second.to_string();
	return true;
}

json summary(const Run& run) {
	json line = json::object();
	line["run_id"] = run.runId;
	line["scene_name"] = run.sceneName;
	line["joined_late"] = !run.sawHello;
	line["base_time_s"] = run.baseTimeSeconds;
	line["trains"] = run.trains;
	line["services"] = run.services;
	line["horizon_steps"] = run.horizonSteps;
	line["states"] = run.states;
	line["first_step"] = run.firstStep;
	line["last_step"] = run.lastStep;
	line["missed"] = run.missed;
	line["positions"] = run.positions;
	line["moving"] = run.moving;
	line["state_bytes"] = run.stateBytes;
	line["signal_frames"] = run.signalFrames;
	line["steps_finished"] = run.stepsFinished ? json(*run.stepsFinished) : json(nullptr);
	line["status"] = run.status ? json(*run.status) : json(nullptr);
	return line;
}

struct Options {
	std::string endpointFile;
	int runs = 1;
	int maxStates = 0;
	int timeoutSeconds = kDefaultTimeoutSeconds;
};

bool readNumber(const char* text, int low, int& value) {
	char* end = nullptr;
	const long number = std::strtol(text, &end, 10);
	if (end == text || *end != '\0' || number < low || number > 1000000)
		return false;
	value = static_cast<int>(number);
	return true;
}

bool readOptions(int argc, char** argv, Options& options) {
	if (argc < 2)
		return false;
	options.endpointFile = argv[1];
	for (int i = 2; i < argc; i += 2) {
		const std::string name = argv[i];
		if (i + 1 >= argc)
			return false;
		if (name == "--runs" && readNumber(argv[i + 1], 0, options.runs))
			continue;
		if (name == "--max-states" && readNumber(argv[i + 1], 1, options.maxStates))
			continue;
		if (name == "--timeout" && readNumber(argv[i + 1], 1, options.timeoutSeconds))
			continue;
		return false;
	}
	return true;
}
} // namespace

int main(int argc, char** argv) {
	Options options;
	if (!readOptions(argc, argv, options)) {
		std::cerr << "usage: live_client ENDPOINT_FILE [--runs N] [--max-states N] [--timeout SECONDS]\n";
		return 2;
	}
	const Clock::time_point deadline = Clock::now() + std::chrono::seconds(options.timeoutSeconds);

	std::optional<Endpoints> endpoints = readEndpoints(options.endpointFile);
	while (!endpoints) {
		if (Clock::now() >= deadline) {
			std::cerr << "cannot read " << options.endpointFile << "\n";
			return 2;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(kReceiveTimeoutMs));
		endpoints = readEndpoints(options.endpointFile);
	}

	zmq::context_t context;
	zmq::socket_t subscriber(context, zmq::socket_type::sub);
	subscriber.set(zmq::sockopt::linger, 0);
	subscriber.set(zmq::sockopt::rcvtimeo, kReceiveTimeoutMs);
	subscriber.set(zmq::sockopt::subscribe, "");
	subscriber.connect(endpoints->state);

	Reader reader;
	std::vector<std::string> problems;
	Clock::time_point lastDescribe = Clock::now() - std::chrono::seconds(1);
	bool timedOut = false;
	for (;;) {
		const bool done = options.runs == 0 ? reader.sawHello() : reader.finishedRuns() >= options.runs;
		if (done || (options.maxStates > 0 && reader.statesRead() >= options.maxStates))
			break;
		if (Clock::now() >= deadline) {
			timedOut = true;
			break;
		}
		if (!reader.sawHello() && !endpoints->command.empty() && Clock::now() - lastDescribe >= std::chrono::milliseconds(kDescribeIntervalMs)) {
			lastDescribe = Clock::now();
			const std::string refusal = describe(context, *endpoints);
			if (!refusal.empty() && refusal != "no_active_run")
				problems.push_back("describe was refused: " + refusal);
		}
		std::string topic;
		std::string text;
		if (receiveFrame(subscriber, topic, text))
			reader.handle(topic, text);
	}

	for (const Run& run : reader.runs())
		std::cout << summary(run).dump() << "\n";
	problems.insert(problems.end(), reader.violations().begin(), reader.violations().end());
	if (timedOut)
		problems.push_back("timed out after " + std::to_string(options.timeoutSeconds) + " seconds");
	for (const std::string& problem : problems)
		std::cerr << "violation: " << problem << "\n";
	return problems.empty() ? 0 : 1;
}
