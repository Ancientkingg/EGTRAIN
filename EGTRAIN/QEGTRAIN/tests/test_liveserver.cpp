// Tests of the live server (io/LiveServer.h) with a synthetic producer and in-process ZeroMQ clients
// on the loopback address.
//
// A subscriber that has just connected misses what is published before its subscription arrives.
// The tests do not wait for a fixed time: they ask the server to describe the run, which publishes
// the hello again, until the hello reaches the subscriber.
//
//   test_liveserver

#include "io/LiveServer.h"

#include <nlohmann/json.hpp>
#include <zmq.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace {
using nlohmann::json;
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

int failures = 0;

void check(bool ok, const std::string& what) {
	if (ok)
		return;
	++failures;
	std::cerr << "failed: " << what << "\n";
}

std::shared_ptr<const std::string> frameText(const json& frame) {
	return std::make_shared<const std::string>(frame.dump());
}

// A frame with the members the tests look at: the kind of frame and its place in the run.
json frameOf(const std::string& kind, int seq, std::size_t padding = 0) {
	json frame = {{"v", 1}, {"run_id", "run"}, {"seq", seq}, {"kind", kind}};
	if (padding > 0)
		frame["padding"] = std::string(padding, 'x');
	return frame;
}

struct Frame {
	std::string topic;
	std::string text;
	json parsed() const { return json::parse(text, nullptr, false); }
	int seq() const {
		const json frame = parsed();
		return frame.is_object() ? frame.value("seq", -1) : -1;
	}
};

std::optional<Frame> receive(zmq::socket_t& socket, int timeoutMs) {
	socket.set(zmq::sockopt::rcvtimeo, timeoutMs);
	zmq::message_t topic;
	if (!socket.recv(topic, zmq::recv_flags::none))
		return std::nullopt;
	zmq::message_t text;
	if (!topic.more() || !socket.recv(text, zmq::recv_flags::none)) {
		check(false, "a frame has two parts");
		return std::nullopt;
	}
	return Frame{topic.to_string(), text.to_string()};
}

// The next frame that is not a hello.
std::optional<Frame> receiveNotHello(zmq::socket_t& socket, int timeoutMs) {
	while (const std::optional<Frame> frame = receive(socket, timeoutMs)) {
		if (frame->topic != kLiveTopicHello)
			return frame;
	}
	return std::nullopt;
}

struct Endpoints {
	std::string state;
	std::string command;
	std::string token;
};

json readFile(const fs::path& file) {
	std::ifstream in(file, std::ios::binary);
	return json::parse(in, nullptr, false);
}

Endpoints readEndpoints(const fs::path& directory) {
	const json file = readFile(directory / "live-endpoint.json");
	Endpoints endpoints;
	if (!file.is_object())
		return endpoints;
	endpoints.state = file.value("state", "");
	endpoints.command = file.value("command", "");
	endpoints.token = file.value("token", "");
	return endpoints;
}

int portOf(const std::string& endpoint) {
	return std::stoi(endpoint.substr(endpoint.rfind(':') + 1));
}

// One subscriber and one requester, with their own context.
class Client {
public:
	explicit Client(const Endpoints& endpoints, int receiveHighWaterMark = 0)
		: m_sub(m_context, zmq::socket_type::sub), m_req(m_context, zmq::socket_type::req), m_token(endpoints.token) {
		m_sub.set(zmq::sockopt::linger, 0);
		m_sub.set(zmq::sockopt::rcvhwm, receiveHighWaterMark);
		m_sub.set(zmq::sockopt::subscribe, "");
		m_sub.connect(endpoints.state);
		m_req.set(zmq::sockopt::linger, 0);
		m_req.set(zmq::sockopt::rcvtimeo, 5000);
		if (!endpoints.command.empty())
			m_req.connect(endpoints.command);
	}

	zmq::socket_t& sub() { return m_sub; }

	// Sends the text as one request and returns the parsed reply, or null when none came.
	json ask(const std::string& text) {
		if (!m_req.send(zmq::buffer(text), zmq::send_flags::none))
			return nullptr;
		zmq::message_t reply;
		if (!m_req.recv(reply, zmq::recv_flags::none))
			return nullptr;
		return json::parse(reply.to_string(), nullptr, false);
	}

	json command(const std::string& id, const std::string& name, const json& args = nullptr) {
		return ask(commandText(m_token, id, name, args));
	}

	static std::string commandText(const std::string& token, const std::string& id, const std::string& name, const json& args = nullptr) {
		json request = {{"v", 1}, {"id", id}, {"token", token}, {"cmd", name}};
		if (!args.is_null())
			request["args"] = args;
		return request.dump();
	}

	const std::string& token() const { return m_token; }

	// Asks for the hello again until it reaches the subscriber, which then has its subscription.
	bool awaitSubscription() {
		for (int attempt = 0; attempt < 100; ++attempt) {
			const json reply = command("describe", "describe");
			if (!reply.is_object() || reply.value("status", "") != "accepted")
				return false;
			const std::optional<Frame> frame = receive(m_sub, 100);
			if (frame && frame->topic == kLiveTopicHello)
				return true;
		}
		return false;
	}

private:
	zmq::context_t m_context;
	zmq::socket_t m_sub;
	zmq::socket_t m_req;
	std::string m_token;
};

class TestDirectory {
public:
	TestDirectory() {
		std::random_device random;
		m_path = fs::temp_directory_path() / ("test_liveserver_" + std::to_string(random()) + "_" + std::to_string(random()));
		fs::create_directories(m_path);
	}
	~TestDirectory() {
		std::error_code ignored;
		fs::remove_all(m_path, ignored);
	}
	TestDirectory(const TestDirectory&) = delete;
	TestDirectory& operator=(const TestDirectory&) = delete;
	fs::path sub(const std::string& name) const { return m_path / name; }

private:
	fs::path m_path;
};

bool startServer(LiveServer& server, const fs::path& directory, bool commands, int port = 0) {
	std::string error;
	const bool started = server.start(port, commands, directory, &error);
	check(started, "the server starts: " + error);
	return started;
}

// --- Tests ---

void testLifecycle(const TestDirectory& directory) {
	const fs::path outputDirectory = directory.sub("lifecycle");
	const fs::path file = outputDirectory / "live-endpoint.json";
	LiveServer server;
	check(!server.running(), "a new server does not run");
	server.pushState(frameText(frameOf("state", 0)));
	check(!server.pushEvent(frameText(frameOf("event", 1))), "an event is refused while the server is not running");
	check(!server.pushHello("run", frameText(frameOf("hello", 2))), "a hello is refused while the server is not running");
	check(!server.tryPopCommand().has_value(), "a stopped server has no command");
	check(server.droppedStates() == 0, "a stopped server drops nothing");
	server.stop();

	std::string error;
	check(!server.start(-1, false, outputDirectory, &error) && !error.empty(), "a negative port is refused with a reason");
	check(!server.start(65536, false, outputDirectory, &error) && !error.empty(), "a port above 65535 is refused with a reason");
	check(!fs::exists(file), "a refused start writes no file");

	if (!startServer(server, outputDirectory, false))
		return;
	const std::string prefix = "tcp://127.0.0.1:";
	check(server.running(), "a started server runs");
	check(server.stateEndpoint().compare(0, prefix.size(), prefix) == 0 && portOf(server.stateEndpoint()) > 0
			&& server.stateEndpoint().find('\0') == std::string::npos,
		"the state socket listens on the loopback address: " + server.stateEndpoint());
	check(server.commandEndpoint().empty(), "no command socket without commands");
	const json content = readFile(file);
	check(content.is_object() && content.value("v", 0) == 1 && content.value("state", "") == server.stateEndpoint() && content["command"].is_null()
			&& content["token"].is_null(),
		"the file names the state endpoint only: " + content.dump());
	check(!server.start(0, false, outputDirectory, &error) && !error.empty(), "a running server does not start again");
	check(server.running(), "the refused second start leaves the server running");

	server.stop();
	check(!server.running(), "a stopped server does not run");
	check(!fs::exists(file), "stop removes the file");
	check(server.stateEndpoint().empty(), "a stopped server has no endpoint");
	server.stop();

	if (!startServer(server, outputDirectory, true))
		return;
	const Endpoints endpoints = readEndpoints(outputDirectory);
	check(!endpoints.command.empty() && endpoints.command == server.commandEndpoint() && endpoints.command != endpoints.state,
		"the file names the command endpoint");
	check(endpoints.token.size() == 32 && endpoints.token.find_first_not_of("0123456789abcdef") == std::string::npos,
		"the token is 128 bits in hex: " + endpoints.token);
#ifndef _WIN32
	const fs::perms others = fs::perms::group_all | fs::perms::others_all;
	check((fs::status(file).permissions() & others) == fs::perms::none, "only the owner can read the file");
#endif
	server.stop();
	if (!startServer(server, outputDirectory, true))
		return;
	check(readEndpoints(outputDirectory).token != endpoints.token, "every start has its own token");
}

// A state, an event and a state again reach the subscriber in the order of the pushes.
void testOrder(const TestDirectory& directory) {
	LiveServer server;
	if (!startServer(server, directory.sub("order"), true))
		return;
	Client client(readEndpoints(directory.sub("order")));
	check(server.pushHello("run", frameText(frameOf("hello", 0))), "the hello is queued");
	check(client.awaitSubscription(), "the subscriber gets the hello");

	server.pushState(frameText(frameOf("state", 1)));
	check(server.pushEvent(frameText(frameOf("event", 2))), "an event is queued");
	server.pushState(frameText(frameOf("state", 3)));
	check(server.pushEvent(frameText(frameOf("event", 4))), "a second event is queued");
	const char* const topics[] = {kLiveTopicState, kLiveTopicEvent, kLiveTopicState, kLiveTopicEvent};
	for (int i = 0; i < 4; ++i) {
		const std::optional<Frame> frame = receiveNotHello(client.sub(), 5000);
		check(frame && frame->seq() == i + 1 && frame->topic == topics[i], "frame " + std::to_string(i + 1) + " arrives in the order of the pushes");
	}
	check(server.droppedStates() == 0, "nothing is dropped below the size of the queue");
}

// A burst larger than the queue keeps the newest state frames, and every state frame is either
// received or counted.
void testDropPolicy(const TestDirectory& directory) {
	LiveServer server;
	if (!startServer(server, directory.sub("drop"), true))
		return;
	Client client(readEndpoints(directory.sub("drop")));
	server.pushHello("run", frameText(frameOf("hello", 0)));
	check(client.awaitSubscription(), "the subscriber gets the hello");

	constexpr int count = 2000;
	std::vector<std::shared_ptr<const std::string>> frames;
	for (int i = 1; i <= count; ++i)
		frames.push_back(frameText(frameOf("state", i)));
	for (const auto& frame : frames)
		server.pushState(frame);
	server.pushEvent(frameText(frameOf("event", count + 1)));

	std::vector<int> seen;
	bool ended = false;
	while (const std::optional<Frame> frame = receiveNotHello(client.sub(), 5000)) {
		if (frame->topic == kLiveTopicEvent) {
			ended = true;
			break;
		}
		seen.push_back(frame->seq());
	}
	check(ended, "the event pushed after the burst arrives");
	bool increasing = true;
	for (std::size_t i = 1; i < seen.size(); ++i)
		increasing = increasing && seen[i] > seen[i - 1];
	check(increasing, "state frames arrive in the order of the pushes");
	check(!seen.empty() && seen.back() == count, "the newest state frame is kept");
	check(seen.size() + server.droppedStates() == static_cast<std::uint64_t>(count),
		"every state frame is received or counted: " + std::to_string(seen.size()) + " and " + std::to_string(server.droppedStates()));
	check(server.droppedStates() > 0, "a burst larger than the queue drops frames");
}

// A subscriber that never reads does not slow the producer, does not take frames from the other
// subscriber, and does not keep the server from stopping.
void testSlowSubscriber(const TestDirectory& directory) {
	LiveServer server;
	if (!startServer(server, directory.sub("slow"), true))
		return;
	const Endpoints endpoints = readEndpoints(directory.sub("slow"));
	Client stuck(endpoints, 1);
	Client reader(endpoints);
	server.pushHello("run", frameText(frameOf("hello", 0)));
	check(stuck.awaitSubscription(), "the subscriber that stops reading has its subscription");
	check(reader.awaitSubscription(), "the reading subscriber has its subscription");

	constexpr int count = 400;
	constexpr int endSeq = count + 1;
	std::atomic<int> received{0};
	std::atomic<bool> ended{false};
	std::atomic<bool> increasing{true};
	std::thread consumer([&] {
		int last = 0;
		while (const std::optional<Frame> frame = receiveNotHello(reader.sub(), 5000)) {
			if (frame->topic == kLiveTopicEvent) {
				ended = (frame->seq() == endSeq);
				return;
			}
			const int seq = frame->seq();
			if (seq <= last)
				increasing = false;
			last = seq;
			++received;
		}
	});
	// Large enough frames to fill the socket buffers of the subscriber that does not read.
	Clock::duration slowestPush{};
	for (int i = 1; i <= count; ++i) {
		const auto frame = frameText(frameOf("state", i, 32768));
		const auto begin = Clock::now();
		server.pushState(frame);
		slowestPush = std::max(slowestPush, Clock::now() - begin);
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	bool queued = false;
	for (int attempt = 0; attempt < 500 && !queued; ++attempt) {
		queued = server.pushEvent(frameText(frameOf("event", endSeq)));
		if (!queued)
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	consumer.join();
	check(queued, "the closing event is queued");
	check(slowestPush < std::chrono::seconds(1), "a push does not wait for a subscriber");
	check(ended.load(), "the subscriber that reads gets the closing event");
	check(increasing.load(), "the subscriber that reads gets its frames in order");
	check(received.load() > 0, "the subscriber that reads gets frames");

	const auto begin = Clock::now();
	server.stop();
	check(Clock::now() - begin < std::chrono::seconds(5), "the server stops with a subscriber that does not read");
}

void testDisconnectAndReconnect(const TestDirectory& directory) {
	LiveServer server;
	if (!startServer(server, directory.sub("reconnect"), true))
		return;
	const Endpoints endpoints = readEndpoints(directory.sub("reconnect"));
	const auto hello = frameText(frameOf("hello", 0));
	server.pushHello("run", hello);
	{
		Client first(endpoints);
		check(first.awaitSubscription(), "the first subscriber has its subscription");
		server.pushEvent(frameText(frameOf("event", 1)));
		const std::optional<Frame> frame = receiveNotHello(first.sub(), 5000);
		check(frame && frame->seq() == 1, "the first subscriber gets an event");
	}
	for (int i = 2; i < 40; ++i)
		server.pushState(frameText(frameOf("state", i)));
	check(server.running(), "the server runs on after a subscriber has gone");

	Client second(endpoints);
	check(second.awaitSubscription(), "a new subscriber has its subscription");
	server.pushEvent(frameText(frameOf("event", 100)));
	bool found = false;
	while (const std::optional<Frame> frame = receiveNotHello(second.sub(), 5000)) {
		if (frame->topic == kLiveTopicEvent) {
			found = frame->seq() == 100;
			break;
		}
	}
	check(found, "a new subscriber gets the frames pushed after it joined");

	// The hello is published again as it was pushed.
	check(second.command("again", "describe").value("status", "") == "accepted", "describe is accepted");
	bool sameHello = false;
	while (const std::optional<Frame> frame = receive(second.sub(), 5000)) {
		if (frame->topic == kLiveTopicHello) {
			sameHello = frame->text == *hello;
			break;
		}
	}
	check(sameHello, "describe publishes the hello text again");
}

void testRestart(const TestDirectory& directory) {
	LiveServer server;
	const fs::path first = directory.sub("restart1");
	if (!startServer(server, first, false))
		return;
	const std::string endpoint = server.stateEndpoint();
	server.stop();

	// The next start can take the port again.
	const fs::path second = directory.sub("restart2");
	if (!startServer(server, second, true, portOf(endpoint)))
		return;
	check(server.stateEndpoint() == endpoint, "a fixed port is the port of the state socket");
	const Endpoints secondEndpoints = readEndpoints(second);
	{
		Client client(secondEndpoints);
		server.pushHello("second", frameText(frameOf("hello", 0)));
		check(client.awaitSubscription(), "a subscriber of the restarted server has its subscription");
		check(client.command("p1", "pause").value("status", "") == "accepted", "a command is accepted");
	}
	check(server.droppedStates() == 0, "no frame was dropped");
	server.stop();

	// Nothing of the earlier run is left: no command, no hello, and a new token.
	const fs::path third = directory.sub("restart3");
	if (!startServer(server, third, true))
		return;
	check(!server.tryPopCommand().has_value(), "a command of the earlier run is gone");
	Client client(readEndpoints(third));
	const json reply = client.command("d1", "describe");
	check(reply.value("reason", "") == "no_active_run", "the hello of the earlier run is gone: " + reply.dump());
	check(readEndpoints(third).token != secondEndpoints.token, "the restarted server has a new token");
}

void testBindFailure(const TestDirectory& directory) {
	zmq::context_t context;
	zmq::socket_t occupant(context, zmq::socket_type::rep);
	occupant.set(zmq::sockopt::linger, 0);
	occupant.bind("tcp://127.0.0.1:*");
	const int port = portOf(occupant.get(zmq::sockopt::last_endpoint));

	const fs::path outputDirectory = directory.sub("bind");
	LiveServer server;
	std::string error;
	check(!server.start(port, false, outputDirectory, &error), "a port in use is refused");
	check(error.find(std::to_string(port)) != std::string::npos, "the reason names the port: " + error);
	check(!server.running(), "a server that failed to start does not run");
	check(!fs::exists(outputDirectory / "live-endpoint.json"), "a server that failed to start writes no file");
	server.stop();
	check(server.start(0, false, outputDirectory, &error), "a server that failed to start can start again: " + error);
}

void expectReply(Client& client, const std::string& text, const char* status, const char* reason, const std::string& what) {
	const json reply = client.ask(text);
	const bool ok = reply.is_object() && reply.value("v", 0) == 1 && reply.value("status", "") == status && reply.value("reason", "") == reason;
	check(ok, what + ": " + reply.dump());
}

void testRequests(const TestDirectory& directory) {
	LiveServer server;
	if (!startServer(server, directory.sub("requests"), true))
		return;
	Client client(readEndpoints(directory.sub("requests")));
	const std::string& token = client.token();
	const auto request = [&](const json& members) {
		json base = {{"v", 1}, {"id", "r"}, {"token", token}, {"cmd", "pause"}};
		for (const auto& member : members.items())
			base[member.key()] = member.value();
		return base.dump();
	};

	expectReply(client, "not json", "rejected", "bad_json", "text that is no JSON");
	expectReply(client, "", "rejected", "bad_json", "an empty request");
	expectReply(client, "[1, 2, 3]", "rejected", "bad_json", "JSON that is no object");
	expectReply(client, "{\"v\":1,", "rejected", "bad_json", "a request that ends early");
	expectReply(client, std::string(kLiveMaxRequestBytes + 1, 'x'), "rejected", "too_large", "a request over the limit is refused before it is parsed");
	expectReply(client, request({{"v", 2}}), "rejected", "bad_version", "another version");
	expectReply(client, request({{"v", "1"}}), "rejected", "bad_version", "a version that is text");
	expectReply(client, request({{"v", 1.5}}), "rejected", "bad_version", "a version that is no integer");
	expectReply(client, request({{"id", nullptr}}), "rejected", "bad_argument", "no id");
	expectReply(client, request({{"id", std::string(kLiveMaxRequestIdBytes + 1, 'i')}}), "rejected", "bad_argument", "an id that is too long");
	expectReply(client, request({{"id", 7}}), "rejected", "bad_argument", "an id that is a number");
	expectReply(client, request({{"token", "0123"}}), "rejected", "bad_token", "a wrong token");
	expectReply(client, request({{"token", nullptr}}), "rejected", "bad_token", "no token");
	expectReply(client, request({{"cmd", "launch"}}), "rejected", "unknown_command", "an unknown command");
	expectReply(client, request({{"cmd", 3}}), "rejected", "unknown_command", "a command that is a number");
	expectReply(client, request({{"cmd", "set_delay_ms"}}), "rejected", "bad_argument", "set_delay_ms without args");
	expectReply(client, request({{"cmd", "set_delay_ms"}, {"args", {{"delay_ms", -1}}}}), "rejected", "bad_argument", "a negative delay");
	expectReply(client, request({{"cmd", "set_delay_ms"}, {"args", {{"delay_ms", kLiveMaxDelayMs + 1}}}}), "rejected", "bad_argument",
		"a delay over the limit");
	expectReply(client, request({{"cmd", "set_delay_ms"}, {"args", {{"delay_ms", 1.5}}}}), "rejected", "bad_argument", "a delay that is no integer");
	expectReply(client, request({{"cmd", "set_delay_ms"}, {"args", {{"delay_ms", "10"}}}}), "rejected", "bad_argument", "a delay that is text");
	check(!server.tryPopCommand().has_value(), "no rejected request is queued");

	const json rejected = client.ask(request({{"id", "echo-1"}, {"token", "wrong"}}));
	check(rejected.value("id", "") == "echo-1", "a reply carries the id of its request: " + rejected.dump());
	const json describeFirst = client.command("d1", "describe");
	check(describeFirst.value("reason", "") == "no_active_run" && describeFirst["run_id"].is_null(), "describe needs a hello: " + describeFirst.dump());
	server.pushHello("run-7", frameText(frameOf("hello", 0)));
	const json describeNext = client.command("d2", "describe");
	check(describeNext.value("status", "") == "accepted" && describeNext.value("run_id", "") == "run-7", "describe is accepted: " + describeNext.dump());

	for (const char* name : {"pause", "resume", "stop"}) {
		const json reply = client.command(std::string("id-") + name, name);
		check(reply.value("status", "") == "accepted" && !reply.contains("reason") && reply.value("id", "") == std::string("id-") + name,
			std::string("accepted: ") + name + " " + reply.dump());
	}
	check(client.command("d3", "set_delay_ms", {{"delay_ms", 0}}).value("status", "") == "accepted", "a delay of 0 is accepted");
	check(client.command("d4", "set_delay_ms", {{"delay_ms", kLiveMaxDelayMs}}).value("status", "") == "accepted", "the largest delay is accepted");
	const LiveCommandKind kinds[] = {LiveCommandKind::Pause, LiveCommandKind::Resume, LiveCommandKind::Stop, LiveCommandKind::SetDelayMs,
		LiveCommandKind::SetDelayMs};
	const char* const ids[] = {"id-pause", "id-resume", "id-stop", "d3", "d4"};
	const int delays[] = {0, 0, 0, 0, kLiveMaxDelayMs};
	for (int i = 0; i < 5; ++i) {
		const std::optional<LiveCommand> command = server.tryPopCommand();
		check(command && command->kind == kinds[i] && command->id == ids[i] && command->delayMs == delays[i],
			"accepted command " + std::to_string(i) + " is queued in order");
	}
	check(!server.tryPopCommand().has_value(), "the queue is empty");

	// Extra parts of a request are ignored, and the next request is answered.
	zmq::context_t context;
	zmq::socket_t multipart(context, zmq::socket_type::req);
	multipart.set(zmq::sockopt::linger, 0);
	multipart.set(zmq::sockopt::rcvtimeo, 5000);
	multipart.connect(server.commandEndpoint());
	const std::string valid = Client::commandText(token, "mp", "pause");
	check(multipart.send(zmq::buffer(valid), zmq::send_flags::sndmore).has_value(), "the first part is sent");
	check(multipart.send(zmq::buffer(std::string("extra")), zmq::send_flags::none).has_value(), "the second part is sent");
	zmq::message_t reply;
	check(multipart.recv(reply, zmq::recv_flags::none).has_value() && json::parse(reply.to_string(), nullptr, false).value("status", "") == "accepted",
		"a request with extra parts is answered");
	expectReply(client, request({{"cmd", "resume"}}), "accepted", "", "a request after the extra parts");

	// A message over the size limit of the socket closes the connection of its sender and gets no reply.
	zmq::socket_t oversized(context, zmq::socket_type::req);
	oversized.set(zmq::sockopt::linger, 0);
	oversized.set(zmq::sockopt::rcvtimeo, 500);
	oversized.connect(server.commandEndpoint());
	check(oversized.send(zmq::buffer(std::string(70000, 'x')), zmq::send_flags::none).has_value(), "the oversized message is sent");
	zmq::message_t none;
	check(!oversized.recv(none, zmq::recv_flags::none).has_value(), "a message over the socket limit gets no reply");
	expectReply(client, request({{"cmd", "resume"}}), "accepted", "", "a request after the oversized message");
	check(server.running(), "the server runs on after the malformed requests");
}

void testCommandQueue(const TestDirectory& directory) {
	LiveServer server;
	if (!startServer(server, directory.sub("queue"), true))
		return;
	Client client(readEndpoints(directory.sub("queue")));
	for (std::size_t i = 0; i < kLiveCommandQueueSize; ++i)
		check(client.command("c" + std::to_string(i), "pause").value("status", "") == "accepted", "command " + std::to_string(i) + " is accepted");
	const json full = client.command("overflow", "pause");
	check(full.value("status", "") == "rejected" && full.value("reason", "") == "queue_full", "a command beyond the queue is refused: " + full.dump());
	for (std::size_t i = 0; i < kLiveCommandQueueSize; ++i) {
		const std::optional<LiveCommand> command = server.tryPopCommand();
		check(command && command->id == "c" + std::to_string(i), "command " + std::to_string(i) + " comes out in order");
	}
	check(!server.tryPopCommand().has_value(), "the refused command was not queued");
	check(client.command("again", "pause").value("status", "") == "accepted", "a command is accepted after the queue was emptied");
}

// The events that were pushed before stop reach a subscriber that is still connected.
void testFinalFlush(const TestDirectory& directory) {
	LiveServer server;
	if (!startServer(server, directory.sub("flush"), true))
		return;
	Client client(readEndpoints(directory.sub("flush")));
	server.pushHello("run", frameText(frameOf("hello", 0)));
	check(client.awaitSubscription(), "the subscriber has its subscription");
	server.pushState(frameText(frameOf("state", 1)));
	server.pushEvent(frameText(frameOf("event", 2)));
	server.pushEvent(frameText(frameOf("event", 3)));
	server.stop();
	for (int expected = 1; expected <= 3; ++expected) {
		const std::optional<Frame> frame = receiveNotHello(client.sub(), 5000);
		check(frame && frame->seq() == expected, "frame " + std::to_string(expected) + " is sent before the server stops");
	}
}

void testShutdownWithClients(const TestDirectory& directory) {
	// Both ways to end a server: stop, and the destructor.
	for (const bool byDestructor : {false, true}) {
		const fs::path outputDirectory = directory.sub(byDestructor ? "shutdown-destructor" : "shutdown-stop");
		auto server = std::make_unique<LiveServer>();
		if (!startServer(*server, outputDirectory, true))
			return;
		const Endpoints endpoints = readEndpoints(outputDirectory);
		Client reading(endpoints);
		Client stuck(endpoints, 1);
		server->pushHello("run", frameText(frameOf("hello", 0)));
		check(reading.awaitSubscription() && stuck.awaitSubscription(), "both subscribers have their subscription");
		for (int i = 1; i <= 50; ++i)
			server->pushState(frameText(frameOf("state", i, 65536)));
		const auto begin = Clock::now();
		if (byDestructor)
			server.reset();
		else
			server->stop();
		check(Clock::now() - begin < std::chrono::seconds(5), "a server with connected clients ends in bounded time");
	}
}

// Four producer threads push states and events while the owner thread takes commands.
void testConcurrentProducers(const TestDirectory& directory) {
	LiveServer server;
	if (!startServer(server, directory.sub("producers"), true))
		return;
	Client client(readEndpoints(directory.sub("producers")));
	server.pushHello("run", frameText(frameOf("hello", 0)));
	check(client.awaitSubscription(), "the subscriber has its subscription");

	constexpr int producers = 4;
	constexpr int perProducer = 300;
	std::atomic<int> states{0};
	std::atomic<int> events{0};
	std::vector<std::thread> threads;
	for (int p = 0; p < producers; ++p) {
		threads.emplace_back([&, p] {
			for (int i = 0; i < perProducer; ++i) {
				server.pushState(frameText(frameOf("state", p * perProducer + i + 1)));
				++states;
				if (i % 10 == 0 && server.pushEvent(frameText(frameOf("event", 100000 + p * perProducer + i))))
					++events;
				if (i % 50 == 0)
					std::this_thread::yield();
			}
		});
	}
	check(client.command("c", "pause").value("status", "") == "accepted", "a command is accepted while the producers push");
	std::optional<LiveCommand> command;
	for (int attempt = 0; attempt < 100 && !command; ++attempt)
		command = server.tryPopCommand();
	check(command.has_value(), "the owner thread takes the command");
	for (std::thread& thread : threads)
		thread.join();
	constexpr int endSeq = 200000;
	bool queued = false;
	for (int attempt = 0; attempt < 500 && !queued; ++attempt) {
		queued = server.pushEvent(frameText(frameOf("event", endSeq)));
		if (!queued)
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	check(queued, "the closing event is queued");

	int receivedStates = 0;
	int receivedEvents = 0;
	bool ended = false;
	while (const std::optional<Frame> frame = receiveNotHello(client.sub(), 5000)) {
		if (frame->topic == kLiveTopicState) {
			++receivedStates;
		} else if (frame->seq() == endSeq) {
			ended = true;
			break;
		} else {
			++receivedEvents;
		}
	}
	check(ended, "the closing event arrives");
	check(receivedEvents == events.load(), "every accepted event arrives: " + std::to_string(receivedEvents) + " of " + std::to_string(events.load()));
	check(static_cast<std::uint64_t>(receivedStates) + server.droppedStates() == static_cast<std::uint64_t>(states.load()),
		"every state is received or counted: " + std::to_string(receivedStates) + " and " + std::to_string(server.droppedStates()));
}
} // namespace

int main() {
	try {
		const TestDirectory directory;
		testLifecycle(directory);
		testOrder(directory);
		testDropPolicy(directory);
		testSlowSubscriber(directory);
		testDisconnectAndReconnect(directory);
		testRestart(directory);
		testBindFailure(directory);
		testRequests(directory);
		testCommandQueue(directory);
		testFinalFlush(directory);
		testShutdownWithClients(directory);
		testConcurrentProducers(directory);
	} catch (const std::exception& error) {
		std::cerr << "failed: exception " << error.what() << "\n";
		return 1;
	}
	return failures == 0 ? 0 : 1;
}
