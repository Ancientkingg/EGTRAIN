// Tests of the live server (io/LiveServer.h) with a synthetic producer and in-process ZeroMQ clients
// on the loopback address.
//
// A subscriber that has just connected misses what is published before its subscription arrives.
// The tests do not wait for a fixed time: they ask the server to describe the run, which publishes
// the hello again, until the hello reaches the subscriber.
//
//   test_liveserver

#include "io/LiveServer.h"

#include <QObject>
#include <QTest>

#include <nlohmann/json.hpp>
#include <zmq.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace {
using nlohmann::json;
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

// A check on the thread of a consumer, or in a helper that returns a value, records its failure here, and cleanup()
// fails the slot that recorded one.
std::mutex recordedMutex;
std::vector<std::string> recordedFailures;

void recordFailure(const std::string& failure) {
	const std::lock_guard<std::mutex> lock(recordedMutex);
	recordedFailures.push_back(failure);
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
		recordFailure("a frame has two parts");
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

// Starts the server. Returns an empty text, or why it did not start.
std::string startServer(LiveServer& server, const fs::path& directory, bool commands, int port = 0) {
	std::string error;
	if (server.start(port, commands, directory, &error))
		return {};
	return "the server starts: " + error;
}

// Sends the text and keeps the reply. True when the reply has the version of the contract, the status and the reason.
bool answers(Client& client, const std::string& text, const char* status, const char* reason, json& reply) {
	reply = client.ask(text);
	return reply.is_object() && reply.value("v", 0) == 1 && reply.value("status", "") == status && reply.value("reason", "") == reason;
}

} // namespace

class TestLiveServer : public QObject {
	Q_OBJECT

private slots:
	void cleanup();
	void lifecycle();
	void order();
	void dropPolicy();
	void slowSubscriber();
	void disconnectAndReconnect();
	void restart();
	void bindFailure();
	void requests();
	void commandQueue();
	void finalFlush();
	void shutdownWithClients_data();
	void shutdownWithClients();
	void concurrentProducers();

private:
	const TestDirectory m_directory;
};

void TestLiveServer::cleanup() {
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

void TestLiveServer::lifecycle() {
	const fs::path outputDirectory = m_directory.sub("lifecycle");
	const fs::path file = outputDirectory / "live-endpoint.json";
	LiveServer server;
	QVERIFY2(!server.running(), "a new server does not run");
	server.pushState(frameText(frameOf("state", 0)));
	QVERIFY2(!server.pushEvent(frameText(frameOf("event", 1))), "an event is refused while the server is not running");
	QVERIFY2(!server.pushHello("run", frameText(frameOf("hello", 2))), "a hello is refused while the server is not running");
	QVERIFY2(!server.tryPopCommand().has_value(), "a stopped server has no command");
	// A stopped server drops nothing.
	QCOMPARE(server.droppedStates(), std::uint64_t(0));
	server.stop();

	std::string error;
	// A negative port is refused with a reason.
	QVERIFY(!server.start(-1, false, outputDirectory, &error));
	QVERIFY(!error.empty());
	// A port above 65535 is refused with a reason.
	QVERIFY(!server.start(65536, false, outputDirectory, &error));
	QVERIFY(!error.empty());
	QVERIFY2(!fs::exists(file), "a refused start writes no file");

	std::string problem = startServer(server, outputDirectory, false);
	QVERIFY2(problem.empty(), problem.c_str());
	const std::string prefix = "tcp://127.0.0.1:";
	QVERIFY2(server.running(), "a started server runs");
	// The state socket listens on the loopback address.
	QCOMPARE(server.stateEndpoint().compare(0, prefix.size(), prefix), 0);
	QCOMPARE_GT(portOf(server.stateEndpoint()), 0);
	QCOMPARE(server.stateEndpoint().find('\0'), std::string::npos);
	QVERIFY2(server.commandEndpoint().empty(), "no command socket without commands");
	const json content = readFile(file);
	QVERIFY2(content.is_object() && content.value("v", 0) == 1 && content.value("state", "") == server.stateEndpoint() && content["command"].is_null()
			&& content["token"].is_null(),
		("the file names the state endpoint only: " + content.dump()).c_str());
	// A running server does not start again.
	QVERIFY(!server.start(0, false, outputDirectory, &error));
	QVERIFY(!error.empty());
	QVERIFY2(server.running(), "the refused second start leaves the server running");

	server.stop();
	QVERIFY2(!server.running(), "a stopped server does not run");
	QVERIFY2(!fs::exists(file), "stop removes the file");
	QVERIFY2(server.stateEndpoint().empty(), "a stopped server has no endpoint");
	server.stop();

	problem = startServer(server, outputDirectory, true);
	QVERIFY2(problem.empty(), problem.c_str());
	const Endpoints endpoints = readEndpoints(outputDirectory);
	// The file names the command endpoint.
	QVERIFY(!endpoints.command.empty());
	QCOMPARE(endpoints.command, server.commandEndpoint());
	QVERIFY(endpoints.command != endpoints.state);
	// The token is 128 bits in hex.
	QCOMPARE(endpoints.token.size(), std::size_t(32));
	QCOMPARE(endpoints.token.find_first_not_of("0123456789abcdef"), std::string::npos);
#ifndef _WIN32
	const fs::perms others = fs::perms::group_all | fs::perms::others_all;
	QVERIFY2((fs::status(file).permissions() & others) == fs::perms::none, "only the owner can read the file");
#endif
	server.stop();
	problem = startServer(server, outputDirectory, true);
	QVERIFY2(problem.empty(), problem.c_str());
	QVERIFY2(readEndpoints(outputDirectory).token != endpoints.token, "every start has its own token");
}

// A state, an event and a state again reach the subscriber in the order of the pushes.
void TestLiveServer::order() {
	LiveServer server;
	const std::string problem = startServer(server, m_directory.sub("order"), true);
	QVERIFY2(problem.empty(), problem.c_str());
	Client client(readEndpoints(m_directory.sub("order")));
	QVERIFY2(server.pushHello("run", frameText(frameOf("hello", 0))), "the hello is queued");
	QVERIFY2(client.awaitSubscription(), "the subscriber gets the hello");

	server.pushState(frameText(frameOf("state", 1)));
	QVERIFY2(server.pushEvent(frameText(frameOf("event", 2))), "an event is queued");
	server.pushState(frameText(frameOf("state", 3)));
	QVERIFY2(server.pushEvent(frameText(frameOf("event", 4))), "a second event is queued");
	const char* const topics[] = {kLiveTopicState, kLiveTopicEvent, kLiveTopicState, kLiveTopicEvent};
	for (int i = 0; i < 4; ++i) {
		const std::optional<Frame> frame = receiveNotHello(client.sub(), 5000);
		// Frame i + 1 arrives in the order of the pushes.
		QVERIFY2(frame.has_value(), ("frame " + std::to_string(i + 1) + " arrives in the order of the pushes").c_str());
		QCOMPARE(frame->seq(), i + 1);
		QCOMPARE(frame->topic, std::string(topics[i]));
	}
	// Nothing is dropped below the size of the queue.
	QCOMPARE(server.droppedStates(), std::uint64_t(0));
}

// A burst larger than the queue keeps the newest state frames, and every state frame is either
// received or counted.
void TestLiveServer::dropPolicy() {
	LiveServer server;
	const std::string problem = startServer(server, m_directory.sub("drop"), true);
	QVERIFY2(problem.empty(), problem.c_str());
	Client client(readEndpoints(m_directory.sub("drop")));
	server.pushHello("run", frameText(frameOf("hello", 0)));
	QVERIFY2(client.awaitSubscription(), "the subscriber gets the hello");

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
	QVERIFY2(ended, "the event pushed after the burst arrives");
	bool increasing = true;
	for (std::size_t i = 1; i < seen.size(); ++i)
		increasing = increasing && seen[i] > seen[i - 1];
	QVERIFY2(increasing, "state frames arrive in the order of the pushes");
	// The newest state frame is kept.
	QVERIFY(!seen.empty());
	QCOMPARE(seen.back(), count);
	// Every state frame is received or counted.
	QCOMPARE(seen.size() + server.droppedStates(), static_cast<std::uint64_t>(count));
	// A burst larger than the queue drops frames.
	QCOMPARE_GT(server.droppedStates(), std::uint64_t(0));
}

// A subscriber that never reads does not slow the producer, does not take frames from the other
// subscriber, and does not keep the server from stopping.
void TestLiveServer::slowSubscriber() {
	LiveServer server;
	const std::string problem = startServer(server, m_directory.sub("slow"), true);
	QVERIFY2(problem.empty(), problem.c_str());
	const Endpoints endpoints = readEndpoints(m_directory.sub("slow"));
	Client stuck(endpoints, 1);
	Client reader(endpoints);
	server.pushHello("run", frameText(frameOf("hello", 0)));
	QVERIFY2(stuck.awaitSubscription(), "the subscriber that stops reading has its subscription");
	QVERIFY2(reader.awaitSubscription(), "the reading subscriber has its subscription");

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
	QVERIFY2(queued, "the closing event is queued");
	QVERIFY2(slowestPush < std::chrono::seconds(1), "a push does not wait for a subscriber");
	QVERIFY2(ended.load(), "the subscriber that reads gets the closing event");
	QVERIFY2(increasing.load(), "the subscriber that reads gets its frames in order");
	// The subscriber that reads gets frames.
	QCOMPARE_GT(received.load(), 0);

	const auto begin = Clock::now();
	server.stop();
	QVERIFY2(Clock::now() - begin < std::chrono::seconds(5), "the server stops with a subscriber that does not read");
}

void TestLiveServer::disconnectAndReconnect() {
	LiveServer server;
	const std::string problem = startServer(server, m_directory.sub("reconnect"), true);
	QVERIFY2(problem.empty(), problem.c_str());
	const Endpoints endpoints = readEndpoints(m_directory.sub("reconnect"));
	const auto hello = frameText(frameOf("hello", 0));
	server.pushHello("run", hello);
	{
		Client first(endpoints);
		QVERIFY2(first.awaitSubscription(), "the first subscriber has its subscription");
		server.pushEvent(frameText(frameOf("event", 1)));
		const std::optional<Frame> frame = receiveNotHello(first.sub(), 5000);
		// The first subscriber gets an event.
		QVERIFY(frame.has_value());
		QCOMPARE(frame->seq(), 1);
	}
	for (int i = 2; i < 40; ++i)
		server.pushState(frameText(frameOf("state", i)));
	QVERIFY2(server.running(), "the server runs on after a subscriber has gone");

	Client second(endpoints);
	QVERIFY2(second.awaitSubscription(), "a new subscriber has its subscription");
	server.pushEvent(frameText(frameOf("event", 100)));
	bool found = false;
	while (const std::optional<Frame> frame = receiveNotHello(second.sub(), 5000)) {
		if (frame->topic == kLiveTopicEvent) {
			found = frame->seq() == 100;
			break;
		}
	}
	QVERIFY2(found, "a new subscriber gets the frames pushed after it joined");

	// The hello is published again as it was pushed.
	// Describe is accepted.
	QCOMPARE(second.command("again", "describe").value("status", ""), std::string("accepted"));
	bool sameHello = false;
	while (const std::optional<Frame> frame = receive(second.sub(), 5000)) {
		if (frame->topic == kLiveTopicHello) {
			sameHello = frame->text == *hello;
			break;
		}
	}
	QVERIFY2(sameHello, "describe publishes the hello text again");
}

void TestLiveServer::restart() {
	LiveServer server;
	const fs::path first = m_directory.sub("restart1");
	std::string problem = startServer(server, first, false);
	QVERIFY2(problem.empty(), problem.c_str());
	const std::string endpoint = server.stateEndpoint();
	server.stop();

	// The next start can take the port again.
	const fs::path second = m_directory.sub("restart2");
	problem = startServer(server, second, true, portOf(endpoint));
	QVERIFY2(problem.empty(), problem.c_str());
	// A fixed port is the port of the state socket.
	QCOMPARE(server.stateEndpoint(), endpoint);
	const Endpoints secondEndpoints = readEndpoints(second);
	{
		Client client(secondEndpoints);
		server.pushHello("second", frameText(frameOf("hello", 0)));
		QVERIFY2(client.awaitSubscription(), "a subscriber of the restarted server has its subscription");
		// A command is accepted.
		QCOMPARE(client.command("p1", "pause").value("status", ""), std::string("accepted"));
	}
	// No frame was dropped.
	QCOMPARE(server.droppedStates(), std::uint64_t(0));
	server.stop();

	// Nothing of the earlier run is left: no command, no hello, and a new token.
	const fs::path third = m_directory.sub("restart3");
	problem = startServer(server, third, true);
	QVERIFY2(problem.empty(), problem.c_str());
	QVERIFY2(!server.tryPopCommand().has_value(), "a command of the earlier run is gone");
	Client client(readEndpoints(third));
	const json reply = client.command("d1", "describe");
	QVERIFY2(reply.value("reason", "") == "no_active_run", ("the hello of the earlier run is gone: " + reply.dump()).c_str());
	QVERIFY2(readEndpoints(third).token != secondEndpoints.token, "the restarted server has a new token");
}

void TestLiveServer::bindFailure() {
	zmq::context_t context;
	zmq::socket_t occupant(context, zmq::socket_type::rep);
	occupant.set(zmq::sockopt::linger, 0);
	occupant.bind("tcp://127.0.0.1:*");
	const int port = portOf(occupant.get(zmq::sockopt::last_endpoint));

	const fs::path outputDirectory = m_directory.sub("bind");
	LiveServer server;
	std::string error;
	QVERIFY2(!server.start(port, false, outputDirectory, &error), "a port in use is refused");
	QVERIFY2(error.find(std::to_string(port)) != std::string::npos, ("the reason names the port: " + error).c_str());
	QVERIFY2(!server.running(), "a server that failed to start does not run");
	QVERIFY2(!fs::exists(outputDirectory / "live-endpoint.json"), "a server that failed to start writes no file");
	server.stop();
	QVERIFY2(server.start(0, false, outputDirectory, &error), ("a server that failed to start can start again: " + error).c_str());
}

void TestLiveServer::requests() {
	LiveServer server;
	const std::string problem = startServer(server, m_directory.sub("requests"), true);
	QVERIFY2(problem.empty(), problem.c_str());
	Client client(readEndpoints(m_directory.sub("requests")));
	const std::string& token = client.token();
	const auto request = [&](const json& members) {
		json base = {{"v", 1}, {"id", "r"}, {"token", token}, {"cmd", "pause"}};
		for (const auto& member : members.items())
			base[member.key()] = member.value();
		return base.dump();
	};

	json reply;
	QVERIFY2(answers(client, "not json", "rejected", "bad_json", reply), ("text that is no JSON: " + reply.dump()).c_str());
	QVERIFY2(answers(client, "", "rejected", "bad_json", reply), ("an empty request: " + reply.dump()).c_str());
	QVERIFY2(answers(client, "[1, 2, 3]", "rejected", "bad_json", reply), ("JSON that is no object: " + reply.dump()).c_str());
	QVERIFY2(answers(client, "{\"v\":1,", "rejected", "bad_json", reply), ("a request that ends early: " + reply.dump()).c_str());
	QVERIFY2(answers(client, std::string(kLiveMaxRequestBytes + 1, 'x'), "rejected", "too_large", reply), ("a request over the limit is refused before it is parsed: " + reply.dump()).c_str());
	QVERIFY2(answers(client, request({{"v", 2}}), "rejected", "bad_version", reply), ("another version: " + reply.dump()).c_str());
	QVERIFY2(answers(client, request({{"v", "1"}}), "rejected", "bad_version", reply), ("a version that is text: " + reply.dump()).c_str());
	QVERIFY2(answers(client, request({{"v", 1.5}}), "rejected", "bad_version", reply), ("a version that is no integer: " + reply.dump()).c_str());
	QVERIFY2(answers(client, request({{"id", nullptr}}), "rejected", "bad_argument", reply), ("no id: " + reply.dump()).c_str());
	QVERIFY2(answers(client, request({{"id", std::string(kLiveMaxRequestIdBytes + 1, 'i')}}), "rejected", "bad_argument", reply), ("an id that is too long: " + reply.dump()).c_str());
	QVERIFY2(answers(client, request({{"id", 7}}), "rejected", "bad_argument", reply), ("an id that is a number: " + reply.dump()).c_str());
	QVERIFY2(answers(client, request({{"token", "0123"}}), "rejected", "bad_token", reply), ("a wrong token: " + reply.dump()).c_str());
	QVERIFY2(answers(client, request({{"token", nullptr}}), "rejected", "bad_token", reply), ("no token: " + reply.dump()).c_str());
	QVERIFY2(answers(client, request({{"cmd", "launch"}}), "rejected", "unknown_command", reply), ("an unknown command: " + reply.dump()).c_str());
	QVERIFY2(answers(client, request({{"cmd", 3}}), "rejected", "unknown_command", reply), ("a command that is a number: " + reply.dump()).c_str());
	QVERIFY2(answers(client, request({{"cmd", "set_delay_ms"}}), "rejected", "bad_argument", reply), ("set_delay_ms without args: " + reply.dump()).c_str());
	QVERIFY2(answers(client, request({{"cmd", "set_delay_ms"}, {"args", {{"delay_ms", -1}}}}), "rejected", "bad_argument", reply), ("a negative delay: " + reply.dump()).c_str());
	QVERIFY2(answers(client, request({{"cmd", "set_delay_ms"}, {"args", {{"delay_ms", kLiveMaxDelayMs + 1}}}}), "rejected", "bad_argument", reply),
		("a delay over the limit: " + reply.dump()).c_str());
	QVERIFY2(answers(client, request({{"cmd", "set_delay_ms"}, {"args", {{"delay_ms", 1.5}}}}), "rejected", "bad_argument", reply), ("a delay that is no integer: " + reply.dump()).c_str());
	QVERIFY2(answers(client, request({{"cmd", "set_delay_ms"}, {"args", {{"delay_ms", "10"}}}}), "rejected", "bad_argument", reply), ("a delay that is text: " + reply.dump()).c_str());
	QVERIFY2(!server.tryPopCommand().has_value(), "no rejected request is queued");

	const json rejected = client.ask(request({{"id", "echo-1"}, {"token", "wrong"}}));
	QVERIFY2(rejected.value("id", "") == "echo-1", ("a reply carries the id of its request: " + rejected.dump()).c_str());
	const json describeFirst = client.command("d1", "describe");
	QVERIFY2(describeFirst.value("reason", "") == "no_active_run" && describeFirst["run_id"].is_null(), ("describe needs a hello: " + describeFirst.dump()).c_str());
	server.pushHello("run-7", frameText(frameOf("hello", 0)));
	const json describeNext = client.command("d2", "describe");
	QVERIFY2(describeNext.value("status", "") == "accepted" && describeNext.value("run_id", "") == "run-7", ("describe is accepted: " + describeNext.dump()).c_str());

	for (const char* name : {"pause", "resume", "stop"}) {
		const json accepted = client.command(std::string("id-") + name, name);
		QVERIFY2(accepted.value("status", "") == "accepted" && !accepted.contains("reason") && accepted.value("id", "") == std::string("id-") + name,
			(std::string("accepted: ") + name + " " + accepted.dump()).c_str());
	}
	// A delay of 0 is accepted.
	QCOMPARE(client.command("d3", "set_delay_ms", {{"delay_ms", 0}}).value("status", ""), std::string("accepted"));
	// The largest delay is accepted.
	QCOMPARE(client.command("d4", "set_delay_ms", {{"delay_ms", kLiveMaxDelayMs}}).value("status", ""), std::string("accepted"));
	const LiveCommandKind kinds[] = {LiveCommandKind::Pause, LiveCommandKind::Resume, LiveCommandKind::Stop, LiveCommandKind::SetDelayMs,
		LiveCommandKind::SetDelayMs};
	const char* const ids[] = {"id-pause", "id-resume", "id-stop", "d3", "d4"};
	const int delays[] = {0, 0, 0, 0, kLiveMaxDelayMs};
	for (int i = 0; i < 5; ++i) {
		const std::optional<LiveCommand> command = server.tryPopCommand();
		// Accepted command i is queued in order.
		QVERIFY2(command.has_value(), ("accepted command " + std::to_string(i) + " is queued in order").c_str());
		QCOMPARE(command->kind, kinds[i]);
		QCOMPARE(command->id, std::string(ids[i]));
		QCOMPARE(command->delayMs, delays[i]);
	}
	QVERIFY2(!server.tryPopCommand().has_value(), "the queue is empty");

	// Extra parts of a request are ignored, and the next request is answered.
	zmq::context_t context;
	zmq::socket_t multipart(context, zmq::socket_type::req);
	multipart.set(zmq::sockopt::linger, 0);
	multipart.set(zmq::sockopt::rcvtimeo, 5000);
	multipart.connect(server.commandEndpoint());
	const std::string valid = Client::commandText(token, "mp", "pause");
	QVERIFY2(multipart.send(zmq::buffer(valid), zmq::send_flags::sndmore).has_value(), "the first part is sent");
	QVERIFY2(multipart.send(zmq::buffer(std::string("extra")), zmq::send_flags::none).has_value(), "the second part is sent");
	zmq::message_t answer;
	// A request with extra parts is answered.
	QVERIFY(multipart.recv(answer, zmq::recv_flags::none).has_value());
	QCOMPARE(json::parse(answer.to_string(), nullptr, false).value("status", ""), std::string("accepted"));
	QVERIFY2(answers(client, request({{"cmd", "resume"}}), "accepted", "", reply), ("a request after the extra parts: " + reply.dump()).c_str());

	// A message over the size limit of the socket closes the connection of its sender and gets no reply.
	zmq::socket_t oversized(context, zmq::socket_type::req);
	oversized.set(zmq::sockopt::linger, 0);
	oversized.set(zmq::sockopt::rcvtimeo, 500);
	oversized.connect(server.commandEndpoint());
	QVERIFY2(oversized.send(zmq::buffer(std::string(70000, 'x')), zmq::send_flags::none).has_value(), "the oversized message is sent");
	zmq::message_t none;
	QVERIFY2(!oversized.recv(none, zmq::recv_flags::none).has_value(), "a message over the socket limit gets no reply");
	QVERIFY2(answers(client, request({{"cmd", "resume"}}), "accepted", "", reply), ("a request after the oversized message: " + reply.dump()).c_str());
	QVERIFY2(server.running(), "the server runs on after the malformed requests");
}

void TestLiveServer::commandQueue() {
	LiveServer server;
	const std::string problem = startServer(server, m_directory.sub("queue"), true);
	QVERIFY2(problem.empty(), problem.c_str());
	Client client(readEndpoints(m_directory.sub("queue")));
	for (std::size_t i = 0; i < kLiveCommandQueueSize; ++i)
		QVERIFY2(client.command("c" + std::to_string(i), "pause").value("status", "") == "accepted", ("command " + std::to_string(i) + " is accepted").c_str());
	const json full = client.command("overflow", "pause");
	QVERIFY2(full.value("status", "") == "rejected" && full.value("reason", "") == "queue_full", ("a command beyond the queue is refused: " + full.dump()).c_str());
	for (std::size_t i = 0; i < kLiveCommandQueueSize; ++i) {
		const std::optional<LiveCommand> command = server.tryPopCommand();
		// Command i comes out in order.
		QVERIFY2(command.has_value(), ("command " + std::to_string(i) + " comes out in order").c_str());
		QCOMPARE(command->id, "c" + std::to_string(i));
	}
	QVERIFY2(!server.tryPopCommand().has_value(), "the refused command was not queued");
	// A command is accepted after the queue was emptied.
	QCOMPARE(client.command("again", "pause").value("status", ""), std::string("accepted"));
}

// The events that were pushed before stop reach a subscriber that is still connected.
void TestLiveServer::finalFlush() {
	LiveServer server;
	const std::string problem = startServer(server, m_directory.sub("flush"), true);
	QVERIFY2(problem.empty(), problem.c_str());
	Client client(readEndpoints(m_directory.sub("flush")));
	server.pushHello("run", frameText(frameOf("hello", 0)));
	QVERIFY2(client.awaitSubscription(), "the subscriber has its subscription");
	server.pushState(frameText(frameOf("state", 1)));
	server.pushEvent(frameText(frameOf("event", 2)));
	server.pushEvent(frameText(frameOf("event", 3)));
	server.stop();
	for (int expected = 1; expected <= 3; ++expected) {
		const std::optional<Frame> frame = receiveNotHello(client.sub(), 5000);
		// Frame expected is sent before the server stops.
		QVERIFY2(frame.has_value(), ("frame " + std::to_string(expected) + " is sent before the server stops").c_str());
		QCOMPARE(frame->seq(), expected);
	}
}

// Both ways to end a server: stop, and the destructor.
void TestLiveServer::shutdownWithClients_data() {
	QTest::addColumn<bool>("byDestructor");
	QTest::newRow("stop") << false;
	QTest::newRow("destructor") << true;
}

void TestLiveServer::shutdownWithClients() {
	QFETCH(bool, byDestructor);
	const fs::path outputDirectory = m_directory.sub(byDestructor ? "shutdown-destructor" : "shutdown-stop");
	auto server = std::make_unique<LiveServer>();
	const std::string problem = startServer(*server, outputDirectory, true);
	QVERIFY2(problem.empty(), problem.c_str());
	const Endpoints endpoints = readEndpoints(outputDirectory);
	Client reading(endpoints);
	Client stuck(endpoints, 1);
	server->pushHello("run", frameText(frameOf("hello", 0)));
	// Both subscribers have their subscription.
	QVERIFY(reading.awaitSubscription());
	QVERIFY(stuck.awaitSubscription());
	for (int i = 1; i <= 50; ++i)
		server->pushState(frameText(frameOf("state", i, 65536)));
	const auto begin = Clock::now();
	if (byDestructor)
		server.reset();
	else
		server->stop();
	QVERIFY2(Clock::now() - begin < std::chrono::seconds(5), "a server with connected clients ends in bounded time");
}

// Four producer threads push states and events while the owner thread takes commands.
void TestLiveServer::concurrentProducers() {
	LiveServer server;
	const std::string problem = startServer(server, m_directory.sub("producers"), true);
	QVERIFY2(problem.empty(), problem.c_str());
	Client client(readEndpoints(m_directory.sub("producers")));
	server.pushHello("run", frameText(frameOf("hello", 0)));
	QVERIFY2(client.awaitSubscription(), "the subscriber has its subscription");

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
	const bool commandAccepted = client.command("c", "pause").value("status", "") == "accepted";
	std::optional<LiveCommand> command;
	for (int attempt = 0; attempt < 100 && !command; ++attempt)
		command = server.tryPopCommand();
	for (std::thread& thread : threads)
		thread.join();
	QVERIFY2(commandAccepted, "a command is accepted while the producers push");
	QVERIFY2(command.has_value(), "the owner thread takes the command");
	constexpr int endSeq = 200000;
	bool queued = false;
	for (int attempt = 0; attempt < 500 && !queued; ++attempt) {
		queued = server.pushEvent(frameText(frameOf("event", endSeq)));
		if (!queued)
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	QVERIFY2(queued, "the closing event is queued");

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
	QVERIFY2(ended, "the closing event arrives");
	// Every accepted event arrives.
	QCOMPARE(receivedEvents, events.load());
	// Every state is received or counted.
	QCOMPARE(static_cast<std::uint64_t>(receivedStates) + server.droppedStates(), static_cast<std::uint64_t>(states.load()));
}

QTEST_APPLESS_MAIN(TestLiveServer)
#include "test_liveserver.moc"
