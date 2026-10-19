#ifndef LIVECOMMAND_H
#define LIVECOMMAND_H

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

/// \file
/// The requests that a client sends to the command socket of the live stream (see io/LiveServer.h) and the
/// checks that decide whether the server accepts one. The functions read text and touch no socket, thread
/// or run, so any thread can call them. No Qt. The requests, replies and reasons are described under
/// Requests in docs/architecture/external-sharing.md.
///
/// The commands are the ones that change the timing of a run or end it: they never change the railway,
/// and a run that receives only pause, resume and set_delay_ms gives the same results as one that
/// receives nothing.

/// Longest request that is parsed, in bytes. A longer one is rejected as too_large.
constexpr std::size_t kLiveMaxRequestBytes = 4096;
/// Longest request id, in bytes. A longer one is rejected as bad_argument.
constexpr std::size_t kLiveMaxRequestIdBytes = 64;
/// Largest delay that set_delay_ms accepts, in milliseconds.
constexpr int kLiveMaxDelayMs = 1000;

/// What a client can ask of a run. The server queues the request and the simulation applies it at the
/// top of a step.
enum class LiveCommandKind {
	Pause,	   ///< "pause": the run waits at the top of a step until a resume or a stop.
	Resume,	   ///< "resume": ends a pause; without a pause it changes nothing.
	Stop,	   ///< "stop": ends the run as cancelled, before its next step.
	SetDelayMs ///< "set_delay_ms": sets the wait at the top of each step to LiveCommand::delayMs.
};

/// A request that passed the checks of the server and waits for the simulation to apply it.
struct LiveCommand {
	LiveCommandKind kind = LiveCommandKind::Pause; ///< The command.
	std::string id;								   ///< The id that the client gave the request, at most kLiveMaxRequestIdBytes bytes.
	int delayMs = 0;							   ///< For SetDelayMs, in milliseconds, 0 to kLiveMaxDelayMs. 0 for the other kinds.
};

/// The name of the command in a request and in the events of a run: "pause", "resume", "stop" or
/// "set_delay_ms". An empty string for a value outside the enumeration.
const char* liveCommandName(LiveCommandKind kind);

/// The outcome of checking the text of one request.
struct LiveRequest {
	/// Why the request is rejected, one of the reasons of the contract, or null when it is accepted.
	/// Points to a string literal.
	const char* reason = nullptr;
	/// The id of the request, once it is known to be a valid id, or empty before that. The reply repeats
	/// it, and sends null when it is empty.
	std::optional<std::string> id;
	/// True when the request asks to describe the run. Otherwise an accepted request holds a command.
	bool describe = false;
	/// The command of an accepted request that is not describe. Its id is the id of the request.
	LiveCommand command;
};

/// Checks the text of a request. It has to be at most kLiveMaxRequestBytes long, a JSON object with the
/// contract version, an id of at most kLiveMaxRequestIdBytes, the token and the name of a command. The
/// first check that fails gives the reason: too_large, bad_json, bad_version, bad_argument, bad_token,
/// unknown_command. set_delay_ms also needs args.delay_ms, a whole number from 0 to kLiveMaxDelayMs, or
/// the reason is bad_argument. The token is compared in a time that depends on its length only.
/// LiveServer calls it on its I/O thread.
LiveRequest parseLiveRequest(std::string_view text, const std::string& token);

#endif // LIVECOMMAND_H
