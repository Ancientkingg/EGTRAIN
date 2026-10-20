#include "io/LiveCommand.h"

#include "io/LiveFrame.h"

#include <nlohmann/json.hpp>

using nlohmann::json;

namespace {
struct NamedCommand {
	const char* name;
	LiveCommandKind kind;
};

constexpr NamedCommand kCommands[] = {
	{"pause", LiveCommandKind::Pause},
	{"resume", LiveCommandKind::Resume},
	{"stop", LiveCommandKind::Stop},
	{"set_delay_ms", LiveCommandKind::SetDelayMs},
};

// Compares in a time that depends on the length only.
bool sameText(const std::string& a, const std::string& b) {
	if (a.size() != b.size())
		return false;
	unsigned char difference = 0;
	for (std::size_t i = 0; i < a.size(); ++i)
		difference = static_cast<unsigned char>(difference | (a[i] ^ b[i]));
	return difference == 0;
}

// The delay of a set_delay_ms request, or nothing when the arguments do not hold one.
std::optional<int> delayOf(const json& request) {
	const auto args = request.find("args");
	if (args == request.end() || !args->is_object())
		return std::nullopt;
	const auto delay = args->find("delay_ms");
	if (delay == args->end() || !delay->is_number_integer() || *delay < 0 || *delay > kLiveMaxDelayMs)
		return std::nullopt;
	return delay->get<int>();
}
} // namespace

const char* liveCommandName(LiveCommandKind kind) {
	for (const NamedCommand& command : kCommands)
		if (command.kind == kind)
			return command.name;
	return "";
}

LiveRequest parseLiveRequest(std::string_view text, const std::string& token) {
	LiveRequest result;
	if (text.size() > kLiveMaxRequestBytes) {
		result.reason = "too_large";
		return result;
	}
	const json request = json::parse(text.begin(), text.end(), nullptr, false);
	if (!request.is_object()) {
		result.reason = "bad_json";
		return result;
	}
	const auto version = request.find("v");
	if (version == request.end() || !version->is_number_integer() || *version != kLiveContractVersion) {
		result.reason = "bad_version";
		return result;
	}
	const auto id = request.find("id");
	if (id == request.end() || !id->is_string() || id->get_ref<const std::string&>().size() > kLiveMaxRequestIdBytes) {
		result.reason = "bad_argument";
		return result;
	}
	result.id = id->get<std::string>();
	const auto sentToken = request.find("token");
	if (sentToken == request.end() || !sentToken->is_string() || !sameText(sentToken->get_ref<const std::string&>(), token)) {
		result.reason = "bad_token";
		return result;
	}
	const auto name = request.find("cmd");
	if (name == request.end() || !name->is_string()) {
		result.reason = "unknown_command";
		return result;
	}
	const std::string& commandName = name->get_ref<const std::string&>();
	if (commandName == "describe") {
		result.describe = true;
		return result;
	}
	for (const NamedCommand& command : kCommands) {
		if (commandName != command.name)
			continue;
		result.command.kind = command.kind;
		result.command.id = *result.id;
		if (command.kind == LiveCommandKind::SetDelayMs) {
			const std::optional<int> delay = delayOf(request);
			if (!delay)
				result.reason = "bad_argument";
			else
				result.command.delayMs = *delay;
		}
		return result;
	}
	result.reason = "unknown_command";
	return result;
}
