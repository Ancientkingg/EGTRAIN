#include "util/Version.h"

#include <charconv>

namespace {

bool parseComponent(const std::string& value, std::size_t& position, unsigned int& output) {
	const char* start = value.data() + position;
	const auto parsed = std::from_chars(start, value.data() + value.size(), output);
	if (parsed.ec != std::errc() || parsed.ptr == start || (parsed.ptr - start > 1 && *start == '0'))
		return false;
	position = static_cast<std::size_t>(parsed.ptr - value.data());
	return true;
}

} // namespace

std::optional<SemanticVersion> parseStableVersion(const std::string& value) {
	SemanticVersion result;
	std::size_t position = 0;
	if (!parseComponent(value, position, result.major)
			|| position >= value.size() || value[position++] != '.'
			|| !parseComponent(value, position, result.minor)
			|| position >= value.size() || value[position++] != '.'
			|| !parseComponent(value, position, result.patch)
			|| position != value.size())
		return std::nullopt;
	return result;
}

std::optional<SemanticVersion> parseStableTag(const std::string& tag) {
	if (tag.size() < 2 || tag.front() != 'v')
		return std::nullopt;
	return parseStableVersion(tag.substr(1));
}
