#ifndef LIVECOMMAND_H
#define LIVECOMMAND_H

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

// The requests that a client sends to the command socket of the live stream (see io/LiveServer.h) and the
// checks that decide whether the server accepts one. The functions read text and touch no socket, thread
// or run. No Qt.
//
// The commands are the ones that change the timing of a run or end it: they never change the railway,
// and a run that receives only pause, resume and set_delay_ms gives the same results as one that
// receives nothing.

// Longest request that is parsed, in bytes, and longest request id.
constexpr std::size_t kLiveMaxRequestBytes = 4096;
constexpr std::size_t kLiveMaxRequestIdBytes = 64;
// Largest delay that set_delay_ms accepts, in milliseconds.
constexpr int kLiveMaxDelayMs = 1000;

// What a client can ask of a run. The server queues the request and the simulation applies it.
enum class LiveCommandKind {
	Pause,
	Resume,
	Stop,
	SetDelayMs
};

// A request that passed the checks of the server and waits for the simulation to apply it.
struct LiveCommand {
	LiveCommandKind kind = LiveCommandKind::Pause;
	// The id that the client gave the request.
	std::string id;
	// For SetDelayMs, in milliseconds, 0 to kLiveMaxDelayMs.
	int delayMs = 0;
};

// The name of the command in a request and in the events of a run: "pause", "resume", "stop" or
// "set_delay_ms".
const char* liveCommandName(LiveCommandKind kind);

// The outcome of checking the text of one request.
struct LiveRequest {
	// Why the request is rejected, one of the reasons of the contract, or null when it is accepted.
	const char* reason = nullptr;
	// The id of the request, once it is known to be a valid id. The reply repeats it.
	std::optional<std::string> id;
	// True when the request asks to describe the run. Otherwise an accepted request holds a command.
	bool describe = false;
	LiveCommand command;
};

// Checks the text of a request. It has to be at most kLiveMaxRequestBytes long, a JSON object with the
// contract version, an id of at most kLiveMaxRequestIdBytes, the token and the name of a command. The
// first check that fails gives the reason: too_large, bad_json, bad_version, bad_argument, bad_token,
// unknown_command. set_delay_ms also needs args.delay_ms, a whole number from 0 to kLiveMaxDelayMs, or
// the reason is bad_argument. The token is compared in a time that depends on its length only.
LiveRequest parseLiveRequest(std::string_view text, const std::string& token);

#endif // LIVECOMMAND_H
