#ifndef EGTRAIN_VERSION_H
#define EGTRAIN_VERSION_H

#include <optional>
#include <string>

struct SemanticVersion {
	unsigned int major = 0;
	unsigned int minor = 0;
	unsigned int patch = 0;

	bool operator==(const SemanticVersion& other) const {
		return major == other.major && minor == other.minor && patch == other.patch;
	}
	bool operator!=(const SemanticVersion& other) const { return !(*this == other); }
	bool operator<(const SemanticVersion& other) const {
		return major < other.major
				|| (major == other.major && minor < other.minor)
				|| (major == other.major && minor == other.minor && patch < other.patch);
	}
};

// Stable versions intentionally exclude prerelease and build metadata.
std::optional<SemanticVersion> parseStableVersion(const std::string& value);
std::optional<SemanticVersion> parseStableTag(const std::string& tag);

#endif
