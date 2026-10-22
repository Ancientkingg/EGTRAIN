#ifndef EGTRAIN_VERSION_H
#define EGTRAIN_VERSION_H

#include <optional>
#include <string>

/// \file
/// Release version numbers of the form MAJOR.MINOR.PATCH.

/// A release version without a prerelease or build part.
struct SemanticVersion {
	unsigned int major = 0; ///< Major number.
	unsigned int minor = 0; ///< Minor number.
	unsigned int patch = 0; ///< Patch number.

	/// True when all three numbers are equal.
	bool operator==(const SemanticVersion& other) const {
		return major == other.major && minor == other.minor && patch == other.patch;
	}
	/// True when any of the three numbers differs.
	bool operator!=(const SemanticVersion& other) const { return !(*this == other); }
	/// Orders by major, then minor, then patch number.
	bool operator<(const SemanticVersion& other) const {
		return major < other.major
			|| (major == other.major && minor < other.minor)
			|| (major == other.major && minor == other.minor && patch < other.patch);
	}
};

/// Reads "MAJOR.MINOR.PATCH": three decimal numbers that fit an unsigned int, without a sign or a
/// leading zero. Stable versions intentionally exclude prerelease and build metadata, so any other
/// text gives no value.
std::optional<SemanticVersion> parseStableVersion(const std::string& value);
/// Reads a release tag: "v" followed by a version that parseStableVersion() accepts, such as
/// "v1.2.3".
std::optional<SemanticVersion> parseStableTag(const std::string& tag);

#endif
