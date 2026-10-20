#ifndef LIVESERVER_H
#define LIVESERVER_H

#include "io/LiveCommand.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

// The transport of the live contract: a ZeroMQ PUB socket that publishes frames to any number of
// subscribers on this machine and, when commands are enabled, a REP socket that takes run-control
// requests. Both sockets listen on 127.0.0.1 only. A server that is not started holds no thread,
// no context and no socket. No Qt.
//
// The server carries frame text that the caller has made (see io/LiveFrame.h) and neither reads
// nor changes it. A published frame is two parts: a topic, then the text.
//
// One I/O thread, started by start(), creates, uses and closes every ZeroMQ socket, because a
// socket must not be shared between threads. Every other thread uses the push functions and
// tryPopCommand. They take a mutex for a few instructions and never wait for the I/O thread, a
// socket or a subscriber, so a step of the simulation cannot stall on the transport.
//
// Frames wait in bounded queues until the I/O thread sends them, in the order in which they were
// pushed. When the state queue is full, the oldest waiting state frame is dropped: a subscriber
// misses it, and sees the gap in the seq of the frames. Hello and event frames are never dropped; a
// push that finds their queue full is refused. A subscriber that does not read loses frames at its
// own high-water mark, without effect on the others.
//
// start and stop are called from one thread at a time. The other functions can be called from any
// thread.

// The first part of a published frame.
inline constexpr char kLiveTopicState[] = "state";
inline constexpr char kLiveTopicHello[] = "hello";
inline constexpr char kLiveTopicEvent[] = "event";

// Number of state frames that can wait for the I/O thread.
constexpr std::size_t kLiveStateQueueSize = 8;
// Number of hello and event frames that can wait for the I/O thread.
constexpr std::size_t kLiveEventQueueSize = 64;
// Number of accepted commands that can wait for tryPopCommand.
constexpr std::size_t kLiveCommandQueueSize = 16;

struct LiveServerState;

class LiveServer {
public:
	LiveServer();
	// Stops the server.
	~LiveServer();
	LiveServer(const LiveServer&) = delete;
	LiveServer& operator=(const LiveServer&) = delete;

	// Starts the I/O thread and binds the sockets. port is the port of the PUB socket on 127.0.0.1;
	// 0 takes a free port. The REP socket, with commandsEnabled, always takes a free port. The
	// endpoints are written to live-endpoint.json in outputDirectory, which is created when it is
	// missing; with commands, the file also holds the token that every request has to carry, and
	// only its owner can read it where the platform supports that. Returns false with the reason in
	// *error when the server is running already, the port is not valid or in use, or the file
	// cannot be written. A server that failed to start holds nothing.
	bool start(int port, bool commandsEnabled, const std::filesystem::path& outputDirectory, std::string* error = nullptr);

	// Sends the frames that are waiting, closes the sockets, and removes live-endpoint.json. The
	// close waits at most half a second for a subscriber that does not read. Does nothing when the
	// server is not running.
	void stop();

	bool running() const;

	// "tcp://127.0.0.1:<port>" of the PUB socket, or empty when the server is not running.
	std::string stateEndpoint() const;
	// The same for the REP socket; empty without commands.
	std::string commandEndpoint() const;

	// Queues a state frame. Does nothing when the server is not running. A full queue drops its
	// oldest state frame first, and droppedStates() counts it.
	void pushState(std::shared_ptr<const std::string> frame);

	// Queues the hello frame of a run and keeps it, with the id of the run, to publish again when a
	// client sends "describe". The latest hello is kept until the server stops. Returns false and
	// queues nothing when the server is not running or its queue is full.
	bool pushHello(const std::string& runId, std::shared_ptr<const std::string> frame);

	// Queues an event frame. Returns false and queues nothing when the server is not running or its
	// queue is full.
	bool pushEvent(std::shared_ptr<const std::string> frame);

	// The oldest accepted command, or nothing. Never waits.
	std::optional<LiveCommand> tryPopCommand();

	// Number of state frames dropped since the server started.
	std::uint64_t droppedStates() const;

private:
	std::unique_ptr<LiveServerState> m_state;
};

#endif // LIVESERVER_H
