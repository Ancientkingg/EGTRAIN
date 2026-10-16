#include "io/LiveServer.h"

#include "io/LiveFrame.h"
#include "util/Log.h"

#include <nlohmann/json.hpp>
#include <zmq.hpp>

#include <atomic>
#include <chrono>
#include <cstring>
#include <deque>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <mutex>
#include <random>
#include <set>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <utility>

using nlohmann::json;
namespace fs = std::filesystem;

namespace {
// An idle I/O thread looks at its queues and its REP socket this often, and a thread that has just
// sent frames looks again after the shorter time.
constexpr int kPollIntervalMs = 10;
constexpr int kBusyPollIntervalMs = 1;
// A closing socket waits this long for a subscriber to take the frames that are left.
constexpr int kLingerMs = 500;
// Messages, not frames: a frame is two. A subscriber that is further behind than this loses frames.
constexpr int kSubscriberHighWaterMark = 1000;
// The REP socket drops the connection of a client that sends a larger message. A request between
// kLiveMaxRequestBytes and this size is answered with too_large.
constexpr std::int64_t kMaxMessageBytes = 65536;
// Requests that are answered before the queued frames are looked at again.
constexpr int kRequestsPerPass = 16;
constexpr char kLoopbackEndpoint[] = "tcp://127.0.0.1:";
constexpr char kEndpointFileName[] = "live-endpoint.json";

struct Queued {
	// Position in the order of the pushes, over both queues.
	std::uint64_t ticket = 0;
	const char* topic = kLiveTopicState;
	std::shared_ptr<const std::string> payload;
};

struct IoConfig {
	int port = 0;
	bool commandsEnabled = false;
	std::string token;
};

struct IoStarted {
	bool ok = false;
	std::string error;
	std::string stateEndpoint;
	std::string commandEndpoint;
};

std::string makeToken() {
	static const char digits[] = "0123456789abcdef";
	std::random_device random;
	std::string token;
	for (int word = 0; word < 4; ++word) {
		const std::uint32_t bits = random();
		for (int shift = 28; shift >= 0; shift -= 4)
			token += digits[(bits >> shift) & 0xF];
	}
	return token;
}

// Compares in a time that depends on the length only.
bool sameText(const std::string& a, const std::string& b) {
	if (a.size() != b.size())
		return false;
	unsigned char difference = 0;
	for (std::size_t i = 0; i < a.size(); ++i)
		difference = static_cast<unsigned char>(difference | (a[i] ^ b[i]));
	return difference == 0;
}

// Creates the file before it holds the token and lets only its owner read it.
bool writeEndpointFile(const fs::path& file, const json& content, std::string& error) {
	std::ofstream out(file, std::ios::binary | std::ios::trunc);
	if (!out) {
		error = "cannot write " + file.string();
		return false;
	}
	std::error_code ignored;
	fs::permissions(file, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace, ignored);
	out << content.dump(2) << "\n";
	out.close();
	if (!out) {
		error = "cannot write " + file.string();
		return false;
	}
	return true;
}
} // namespace

struct LiveServerState {
	std::mutex mutex;
	// Guarded by mutex.
	bool accepting = false;
	std::uint64_t nextTicket = 0;
	std::deque<Queued> states;
	std::deque<Queued> events;
	std::deque<LiveCommand> commands;
	std::shared_ptr<const std::string> hello;
	std::string helloRunId;
	std::string stateEndpoint;
	std::string commandEndpoint;
	// Read by the I/O thread, written by the thread that stops the server.
	std::atomic<bool> stopRequested{false};
	std::atomic<std::uint64_t> droppedStates{0};
	// Touched by the thread that starts and stops the server only.
	std::thread thread;
	fs::path endpointFile;
};

namespace {
// The I/O thread. It creates the sockets, so it owns them; the context is declared first and
// outlives them.
class IoLoop {
public:
	IoLoop(LiveServerState& state, const IoConfig& config)
		: m_state(state), m_token(config.token), m_pub(m_context, zmq::socket_type::pub) {
		m_pub.set(zmq::sockopt::linger, kLingerMs);
		m_pub.set(zmq::sockopt::sndhwm, kSubscriberHighWaterMark);
		bindLoopback(m_pub, config.port == 0 ? "*" : std::to_string(config.port));
		m_stateEndpoint = m_pub.get(zmq::sockopt::last_endpoint);
		if (config.commandsEnabled) {
			m_rep = zmq::socket_t(m_context, zmq::socket_type::rep);
			m_rep.set(zmq::sockopt::linger, kLingerMs);
			m_rep.set(zmq::sockopt::maxmsgsize, kMaxMessageBytes);
			bindLoopback(m_rep, "*");
			m_commandEndpoint = m_rep.get(zmq::sockopt::last_endpoint);
		}
	}

	// Closes every socket, then the context.
	~IoLoop() {
		m_rep.close();
		m_pub.close();
	}

	IoLoop(const IoLoop&) = delete;
	IoLoop& operator=(const IoLoop&) = delete;

	const std::string& stateEndpoint() const { return m_stateEndpoint; }
	const std::string& commandEndpoint() const { return m_commandEndpoint; }

	// Sends the queued frames and answers requests until stop is requested, then sends what is left.
	void run() {
		for (;;) {
			const bool stopping = m_state.stopRequested.load();
			const bool busy = sendQueued();
			if (stopping)
				return;
			serveCommands(busy);
		}
	}

private:
	static void bindLoopback(zmq::socket_t& socket, const std::string& port) {
		const std::string endpoint = std::string(kLoopbackEndpoint) + port;
		try {
			socket.bind(endpoint);
		} catch (const zmq::error_t& error) {
			throw std::runtime_error("cannot listen on " + endpoint + ": " + error.what());
		}
	}

	// Writes one message to the console per kind of problem.
	void report(const std::string& what) {
		if (m_reported.insert(what).second) {
			EG_LOG(IoZmq, Warning) << "Live server: " << what << "\n";
		}
	}

	void publish(const char* topic, const std::string& text) {
		try {
			const bool sent = m_pub.send(zmq::buffer(topic, std::strlen(topic)), zmq::send_flags::sndmore | zmq::send_flags::dontwait).has_value()
				&& m_pub.send(zmq::buffer(text), zmq::send_flags::dontwait).has_value();
			if (!sent)
				report("a frame could not be handed to the state socket");
		} catch (const std::exception& error) {
			report(std::string("sending a frame failed: ") + error.what());
		}
	}

	// Sends every waiting frame in the order of the pushes. Returns whether there was one.
	bool sendQueued() {
		std::deque<Queued> states;
		std::deque<Queued> events;
		{
			std::lock_guard<std::mutex> lock(m_state.mutex);
			states.swap(m_state.states);
			events.swap(m_state.events);
		}
		const bool any = !states.empty() || !events.empty();
		while (!states.empty() || !events.empty()) {
			const bool stateFirst = events.empty() || (!states.empty() && states.front().ticket < events.front().ticket);
			std::deque<Queued>& next = stateFirst ? states : events;
			publish(next.front().topic, *next.front().payload);
			next.pop_front();
		}
		return any;
	}

	// Answers the requests that have arrived. The first receive waits for a poll interval, which
	// paces the thread. Without a command socket the thread only waits.
	void serveCommands(bool busy) {
		const auto wait = std::chrono::milliseconds(busy ? kBusyPollIntervalMs : kPollIntervalMs);
		if (!m_rep) {
			std::this_thread::sleep_for(wait);
			return;
		}
		try {
			m_rep.set(zmq::sockopt::rcvtimeo, static_cast<int>(wait.count()));
			zmq::recv_flags flags = zmq::recv_flags::none;
			for (int served = 0; served < kRequestsPerPass; ++served) {
				zmq::message_t request;
				if (!m_rep.recv(request, flags))
					return;
				answer(request);
				flags = zmq::recv_flags::dontwait;
			}
		} catch (const std::exception& error) {
			report(std::string("serving a request failed: ") + error.what());
			std::this_thread::sleep_for(wait);
		}
	}

	void answer(zmq::message_t& request) {
		const std::string_view text(request.data<char>(), request.size());
		const std::string reply = judge(text);
		for (bool more = request.more(); more;) {
			zmq::message_t extra;
			if (!m_rep.recv(extra, zmq::recv_flags::dontwait))
				break;
			more = extra.more();
		}
		if (!m_rep.send(zmq::buffer(reply), zmq::send_flags::dontwait))
			report("a reply could not be handed to the command socket");
	}

	// Checks one request and acts on it. Returns the reply.
	std::string judge(std::string_view text) {
		json id = nullptr;
		const char* reason = check(text, id);
		json reply = json::object();
		reply["v"] = kLiveContractVersion;
		reply["id"] = id;
		reply["status"] = reason ? "rejected" : "accepted";
		if (reason)
			reply["reason"] = reason;
		{
			std::lock_guard<std::mutex> lock(m_state.mutex);
			reply["run_id"] = m_state.hello ? json(m_state.helloRunId) : json(nullptr);
		}
		return reply.dump(-1, ' ', false, json::error_handler_t::replace);
	}

	// Returns the reason to reject the request, or null when it is accepted. id is the id of the
	// request once it is known to be valid.
	const char* check(std::string_view text, json& id) {
		if (text.size() > kLiveMaxRequestBytes)
			return "too_large";
		const json request = json::parse(text.begin(), text.end(), nullptr, false);
		if (!request.is_object())
			return "bad_json";
		const auto version = request.find("v");
		if (version == request.end() || !version->is_number_integer() || *version != kLiveContractVersion)
			return "bad_version";
		const auto requestId = request.find("id");
		if (requestId == request.end() || !requestId->is_string() || requestId->get_ref<const std::string&>().size() > kLiveMaxRequestIdBytes)
			return "bad_argument";
		id = *requestId;
		const auto token = request.find("token");
		if (token == request.end() || !token->is_string() || !sameText(token->get_ref<const std::string&>(), m_token))
			return "bad_token";
		const auto command = request.find("cmd");
		if (command == request.end() || !command->is_string())
			return "unknown_command";
		const std::string& name = command->get_ref<const std::string&>();
		if (name == "describe")
			return describe();
		if (name == "pause")
			return enqueue({LiveCommandKind::Pause, id.get<std::string>(), 0});
		if (name == "resume")
			return enqueue({LiveCommandKind::Resume, id.get<std::string>(), 0});
		if (name == "stop")
			return enqueue({LiveCommandKind::Stop, id.get<std::string>(), 0});
		if (name != "set_delay_ms")
			return "unknown_command";
		const auto args = request.find("args");
		if (args == request.end() || !args->is_object())
			return "bad_argument";
		const auto delay = args->find("delay_ms");
		if (delay == args->end() || !delay->is_number_integer() || *delay < 0 || *delay > kLiveMaxDelayMs)
			return "bad_argument";
		return enqueue({LiveCommandKind::SetDelayMs, id.get<std::string>(), delay->get<int>()});
	}

	// Publishes the latest hello again, so that a subscriber that has just connected gets it.
	const char* describe() {
		std::shared_ptr<const std::string> hello;
		{
			std::lock_guard<std::mutex> lock(m_state.mutex);
			hello = m_state.hello;
		}
		if (!hello)
			return "no_active_run";
		publish(kLiveTopicHello, *hello);
		return nullptr;
	}

	const char* enqueue(LiveCommand command) {
		std::lock_guard<std::mutex> lock(m_state.mutex);
		if (m_state.commands.size() >= kLiveCommandQueueSize)
			return "queue_full";
		m_state.commands.push_back(std::move(command));
		return nullptr;
	}

	LiveServerState& m_state;
	const std::string m_token;
	zmq::context_t m_context;
	zmq::socket_t m_pub;
	zmq::socket_t m_rep;
	std::string m_stateEndpoint;
	std::string m_commandEndpoint;
	std::set<std::string> m_reported;
};

void ioMain(LiveServerState& state, IoConfig config, std::promise<IoStarted> started) {
	bool answered = false;
	try {
		IoLoop loop(state, config);
		IoStarted result;
		result.ok = true;
		result.stateEndpoint = loop.stateEndpoint();
		result.commandEndpoint = loop.commandEndpoint();
		started.set_value(std::move(result));
		answered = true;
		loop.run();
	} catch (const std::exception& error) {
		if (!answered) {
			IoStarted failed;
			failed.error = error.what();
			started.set_value(std::move(failed));
		} else {
			EG_LOG(IoZmq, Error) << "Live server stopped: " << error.what() << "\n";
		}
	}
	std::lock_guard<std::mutex> lock(state.mutex);
	state.accepting = false;
}
} // namespace

LiveServer::LiveServer() : m_state(std::make_unique<LiveServerState>()) {}

LiveServer::~LiveServer() {
	stop();
}

bool LiveServer::start(int port, bool commandsEnabled, const fs::path& outputDirectory, std::string* error) {
	std::string unused;
	std::string& message = error ? *error : unused;
	message.clear();
	if (running()) {
		message = "the live server is running";
		return false;
	}
	// Joins an I/O thread that has ended by itself.
	stop();
	if (port < 0 || port > 65535) {
		message = "port " + std::to_string(port) + " is not valid";
		return false;
	}
	try {
		{
			std::lock_guard<std::mutex> lock(m_state->mutex);
			m_state->states.clear();
			m_state->events.clear();
			m_state->commands.clear();
			m_state->hello.reset();
			m_state->helloRunId.clear();
		}
		m_state->droppedStates = 0;
		m_state->stopRequested = false;

		IoConfig config;
		config.port = port;
		config.commandsEnabled = commandsEnabled;
		if (commandsEnabled)
			config.token = makeToken();
		std::promise<IoStarted> started;
		std::future<IoStarted> result = started.get_future();
		m_state->thread = std::thread(ioMain, std::ref(*m_state), config, std::move(started));
		const IoStarted bound = result.get();
		if (!bound.ok) {
			m_state->thread.join();
			message = bound.error;
			return false;
		}

		json endpoints = json::object();
		endpoints["v"] = kLiveContractVersion;
		endpoints["state"] = bound.stateEndpoint;
		endpoints["command"] = commandsEnabled ? json(bound.commandEndpoint) : json(nullptr);
		endpoints["token"] = commandsEnabled ? json(config.token) : json(nullptr);
		std::error_code directoryError;
		fs::create_directories(outputDirectory, directoryError);
		m_state->endpointFile = outputDirectory / kEndpointFileName;
		if (!writeEndpointFile(m_state->endpointFile, endpoints, message)) {
			stop();
			return false;
		}
		std::lock_guard<std::mutex> lock(m_state->mutex);
		m_state->stateEndpoint = bound.stateEndpoint;
		m_state->commandEndpoint = commandsEnabled ? bound.commandEndpoint : std::string();
		m_state->accepting = true;
	} catch (const std::exception& failure) {
		message = failure.what();
		stop();
		return false;
	}
	return true;
}

void LiveServer::stop() {
	if (!m_state->thread.joinable())
		return;
	{
		std::lock_guard<std::mutex> lock(m_state->mutex);
		m_state->accepting = false;
	}
	m_state->stopRequested = true;
	m_state->thread.join();
	std::error_code ignored;
	fs::remove(m_state->endpointFile, ignored);
	std::lock_guard<std::mutex> lock(m_state->mutex);
	m_state->stateEndpoint.clear();
	m_state->commandEndpoint.clear();
}

bool LiveServer::running() const {
	std::lock_guard<std::mutex> lock(m_state->mutex);
	return m_state->accepting;
}

std::string LiveServer::stateEndpoint() const {
	std::lock_guard<std::mutex> lock(m_state->mutex);
	return m_state->stateEndpoint;
}

std::string LiveServer::commandEndpoint() const {
	std::lock_guard<std::mutex> lock(m_state->mutex);
	return m_state->commandEndpoint;
}

void LiveServer::pushState(std::shared_ptr<const std::string> frame) {
	if (!frame)
		return;
	std::lock_guard<std::mutex> lock(m_state->mutex);
	if (!m_state->accepting)
		return;
	if (m_state->states.size() >= kLiveStateQueueSize) {
		m_state->states.pop_front();
		++m_state->droppedStates;
	}
	m_state->states.push_back({m_state->nextTicket++, kLiveTopicState, std::move(frame)});
}

bool LiveServer::pushHello(const std::string& runId, std::shared_ptr<const std::string> frame) {
	if (!frame)
		return false;
	std::lock_guard<std::mutex> lock(m_state->mutex);
	if (!m_state->accepting || m_state->events.size() >= kLiveEventQueueSize)
		return false;
	m_state->events.push_back({m_state->nextTicket++, kLiveTopicHello, frame});
	m_state->hello = std::move(frame);
	m_state->helloRunId = runId;
	return true;
}

bool LiveServer::pushEvent(std::shared_ptr<const std::string> frame) {
	if (!frame)
		return false;
	std::lock_guard<std::mutex> lock(m_state->mutex);
	if (!m_state->accepting || m_state->events.size() >= kLiveEventQueueSize)
		return false;
	m_state->events.push_back({m_state->nextTicket++, kLiveTopicEvent, std::move(frame)});
	return true;
}

std::optional<LiveCommand> LiveServer::tryPopCommand() {
	std::lock_guard<std::mutex> lock(m_state->mutex);
	if (m_state->commands.empty())
		return std::nullopt;
	LiveCommand command = std::move(m_state->commands.front());
	m_state->commands.pop_front();
	return command;
}

std::uint64_t LiveServer::droppedStates() const {
	return m_state->droppedStates.load();
}
