// Tests of the checks that the command socket makes on a request (io/LiveCommand.h) and of the event
// frame that reports a command (io/LiveFrame.h). No socket, thread or run.
//
//   test_livecommand

#include "io/LiveCommand.h"
#include "io/LiveFrame.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <iostream>
#include <string>

namespace {
using nlohmann::json;

int failures = 0;

void check(bool ok, const std::string& what) {
	if (ok)
		return;
	++failures;
	std::cerr << "failed: " << what << "\n";
}

const char* const kToken = "0123456789abcdef0123456789abcdef";

// A request that passes every check, with the members of `changes` replacing or adding to it. A null
// member in `changes` removes the member.
std::string request(const json& changes = json::object()) {
	json base = {{"v", 1}, {"id", "r1"}, {"token", kToken}, {"cmd", "pause"}};
	for (auto member = changes.begin(); member != changes.end(); ++member) {
		if (member.value().is_null())
			base.erase(member.key());
		else
			base[member.key()] = member.value();
	}
	return base.dump();
}

std::string reasonOf(const std::string& text) {
	const LiveRequest result = parseLiveRequest(text, kToken);
	return result.reason ? result.reason : "";
}

void checkReason(const std::string& text, const std::string& expected, const std::string& what) {
	const std::string actual = reasonOf(text);
	check(actual == expected, what + ": the reason is '" + actual + "' and not '" + expected + "'");
}

void testAcceptedCommands() {
	const struct {
		const char* name;
		LiveCommandKind kind;
	} commands[] = {
		{"pause", LiveCommandKind::Pause},
		{"resume", LiveCommandKind::Resume},
		{"stop", LiveCommandKind::Stop},
	};
	for (const auto& command : commands) {
		const LiveRequest result = parseLiveRequest(request({{"cmd", command.name}}), kToken);
		check(!result.reason && !result.describe && result.command.kind == command.kind && result.command.id == "r1"
				&& result.id == std::string("r1"),
			std::string("accepted ") + command.name);
		check(std::string(liveCommandName(command.kind)) == command.name, std::string("the name of ") + command.name);
	}
	const LiveRequest describe = parseLiveRequest(request({{"cmd", "describe"}}), kToken);
	check(!describe.reason && describe.describe && describe.id == std::string("r1"), "describe is accepted and is no command");

	for (const int delay : {0, 1, 250, kLiveMaxDelayMs}) {
		const LiveRequest result = parseLiveRequest(request({{"cmd", "set_delay_ms"}, {"args", {{"delay_ms", delay}}}}), kToken);
		check(!result.reason && result.command.kind == LiveCommandKind::SetDelayMs && result.command.delayMs == delay
				&& result.command.id == "r1",
			"set_delay_ms " + std::to_string(delay));
	}
	check(std::string(liveCommandName(LiveCommandKind::SetDelayMs)) == "set_delay_ms", "the name of set_delay_ms");

	const LiveRequest extra = parseLiveRequest(request({{"cmd", "stop"}, {"args", {{"delay_ms", 5}}}, {"note", "x"}}), kToken);
	check(!extra.reason && extra.command.kind == LiveCommandKind::Stop && extra.command.delayMs == 0,
		"members the contract does not know are ignored");
}

void testRejections() {
	checkReason("", "bad_json", "an empty request");
	checkReason("pause", "bad_json", "text that is no JSON");
	checkReason("{\"v\":1", "bad_json", "JSON that ends early");
	checkReason("[1,2]", "bad_json", "JSON that is no object");
	checkReason("3", "bad_json", "a number");
	checkReason(std::string("{\"v\":1,\"id\":\"r\",\"token\":\"") + kToken + "\",\"cmd\":\"pause\"}\xff", "bad_json", "bytes after the object");

	checkReason(request({{"v", nullptr}}), "bad_version", "no version");
	checkReason(request({{"v", 2}}), "bad_version", "another version");
	checkReason(request({{"v", 0}}), "bad_version", "version 0");
	checkReason(request({{"v", "1"}}), "bad_version", "a version that is text");
	checkReason(request({{"v", 1.5}}), "bad_version", "a version that is no integer");

	checkReason(request({{"id", nullptr}}), "bad_argument", "no id");
	checkReason(request({{"id", 7}}), "bad_argument", "an id that is a number");
	checkReason(request({{"id", std::string(kLiveMaxRequestIdBytes + 1, 'x')}}), "bad_argument", "an id of 65 bytes");
	checkReason(request({{"id", std::string(kLiveMaxRequestIdBytes, 'x')}}), "", "an id of 64 bytes");
	checkReason(request({{"id", ""}}), "", "an empty id");

	checkReason(request({{"token", nullptr}}), "bad_token", "no token");
	checkReason(request({{"token", 5}}), "bad_token", "a token that is a number");
	checkReason(request({{"token", ""}}), "bad_token", "an empty token");
	checkReason(request({{"token", std::string(kToken) + "0"}}), "bad_token", "a token that is too long");
	checkReason(request({{"token", std::string(kToken).substr(1)}}), "bad_token", "a token that is too short");
	checkReason(request({{"token", "1123456789abcdef0123456789abcdef"}}), "bad_token", "a token that differs in the first digit");
	checkReason(request({{"token", "0123456789abcdef0123456789abcdee"}}), "bad_token", "a token that differs in the last digit");

	checkReason(request({{"cmd", nullptr}}), "unknown_command", "no command");
	checkReason(request({{"cmd", 3}}), "unknown_command", "a command that is a number");
	checkReason(request({{"cmd", "launch"}}), "unknown_command", "an unknown command");
	checkReason(request({{"cmd", "Pause"}}), "unknown_command", "a command in capitals");
	checkReason(request({{"cmd", "set_speed"}}), "unknown_command", "a command that would change the railway");

	const json delayCommand = {{"cmd", "set_delay_ms"}};
	checkReason(request(delayCommand), "bad_argument", "set_delay_ms without args");
	checkReason(request({{"cmd", "set_delay_ms"}, {"args", 5}}), "bad_argument", "args that is no object");
	checkReason(request({{"cmd", "set_delay_ms"}, {"args", json::object()}}), "bad_argument", "args without delay_ms");
	checkReason(request({{"cmd", "set_delay_ms"}, {"args", {{"delay_ms", -1}}}}), "bad_argument", "a negative delay");
	checkReason(request({{"cmd", "set_delay_ms"}, {"args", {{"delay_ms", kLiveMaxDelayMs + 1}}}}), "bad_argument", "a delay above the limit");
	checkReason(request({{"cmd", "set_delay_ms"}, {"args", {{"delay_ms", 1.5}}}}), "bad_argument", "a delay that is no integer");
	checkReason(request({{"cmd", "set_delay_ms"}, {"args", {{"delay_ms", "10"}}}}), "bad_argument", "a delay that is text");
	checkReason(request({{"cmd", "set_delay_ms"}, {"args", {{"delay_ms", nullptr}}}}), "bad_argument", "a delay that is null");
}

// The first check that fails gives the reason, so a reply never says more than the first fault.
void testOrderOfChecks() {
	checkReason(request({{"v", 2}, {"id", 7}, {"token", "x"}, {"cmd", "launch"}}), "bad_version", "version before id, token and command");
	checkReason(request({{"id", 7}, {"token", "x"}, {"cmd", "launch"}}), "bad_argument", "id before token and command");
	checkReason(request({{"token", "x"}, {"cmd", "launch"}}), "bad_token", "token before command");
	checkReason(request({{"token", "x"}, {"cmd", "set_delay_ms"}}), "bad_token", "token before the arguments");
	checkReason(request({{"cmd", "launch"}, {"args", 5}}), "unknown_command", "command before the arguments");

	// A request that is too large is refused before it is parsed: it does not have to be JSON.
	checkReason(std::string(kLiveMaxRequestBytes + 1, 'x'), "too_large", "text over the limit");
	checkReason(request({{"padding", std::string(kLiveMaxRequestBytes, 'x')}}), "too_large", "a valid request over the limit");
	std::string exact = request();
	const std::size_t padding = kLiveMaxRequestBytes - exact.size() - std::string(",\"padding\":\"\"").size();
	exact = request({{"padding", std::string(padding, 'x')}});
	check(exact.size() == kLiveMaxRequestBytes, "the request at the limit has the size of the limit");
	checkReason(exact, "", "a request of exactly the limit");
}

// The reply repeats the id of a request once it is valid, whatever fails later.
void testIdInTheReply() {
	check(!parseLiveRequest(request({{"v", 2}}), kToken).id, "no id comes back from a request with another version");
	check(!parseLiveRequest(request({{"id", 7}}), kToken).id, "no id comes back from an invalid id");
	check(!parseLiveRequest("not json", kToken).id, "no id comes back from text that is no JSON");
	check(parseLiveRequest(request({{"token", "x"}}), kToken).id == std::string("r1"), "the id comes back with a wrong token");
	check(parseLiveRequest(request({{"cmd", "launch"}}), kToken).id == std::string("r1"), "the id comes back with an unknown command");
}

void testCommandEventFrames() {
	LiveRunInfo run;
	run.runId = "run-1";
	LiveCommand command;
	command.kind = LiveCommandKind::SetDelayMs;
	command.id = "client-7";
	command.delayMs = 40;

	const json applied = makeCommandEventFrame(run, 12, command, 300);
	check(applied == json({{"v", kLiveContractVersion}, {"run_id", "run-1"}, {"seq", 12}, {"kind", "applied"}, {"step", 300}, {"command_id", "client-7"}, {"cmd", "set_delay_ms"}}),
		"applied frame: " + applied.dump());

	command.kind = LiveCommandKind::Stop;
	const json rejected = makeCommandEventFrame(run, 13, command, 1500, "run_ended");
	check(rejected == json({{"v", kLiveContractVersion}, {"run_id", "run-1"}, {"seq", 13}, {"kind", "rejected"}, {"step", 1500}, {"command_id", "client-7"}, {"cmd", "stop"}, {"reason", "run_ended"}}),
		"rejected frame: " + rejected.dump());
}
} // namespace

int main() {
	testAcceptedCommands();
	testRejections();
	testOrderOfChecks();
	testIdInTheReply();
	testCommandEventFrames();
	if (failures)
		std::cerr << failures << " checks failed\n";
	return failures ? 1 : 0;
}
