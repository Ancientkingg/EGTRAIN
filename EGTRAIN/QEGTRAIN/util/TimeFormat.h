#ifndef TIMEFORMAT_H
#define TIMEFORMAT_H

#include <string>
#include <optional>

/// \file
/// Text forms of simulation and planned times.

/// Formats a simulation time as a time of day "HH:MM:SS".
/// \p simSeconds is seconds since the start of the run, and \p baseOffsetSeconds the start of the
/// run in seconds since midnight. A negative sum gives "00:00:00", and the result wraps at 24 h.
std::string formatSimTime(long long simSeconds, long long baseOffsetSeconds = 0);

/// Formats a planned time, \p value seconds after the base time \p baseOffsetSeconds (seconds since
/// midnight). An empty \p value gives "". A non-finite \p value, or a base outside [0, 86400),
/// gives "Invalid". When \p clock is false the result is \p value in seconds; when it is true, the
/// time of day "HH:MM:SS" of base plus \p value, preceded by "+Nd " or "-Nd " on another day than
/// the base. Seconds keep their fraction at full precision without trailing zeros, and the decimal
/// point does not depend on the locale.
std::string formatPlannedTime(std::optional<double> value, bool clock, long long baseOffsetSeconds);
/// Reads a planned time in the form of formatPlannedTime() into \p value, in seconds after the
/// base. Empty \p text clears \p value. The clock form accepts an optional "+Nd " prefix but no
/// negative day, hours up to 23, minutes up to 59 and seconds below 60. When \p text equals the
/// form of the current \p value, \p value keeps its exact stored value. Returns false and leaves
/// \p value unchanged when the text is malformed, the result is negative or the base is outside
/// [0, 86400).
bool parsePlannedTime(const std::string& text, bool clock, long long baseOffsetSeconds,
	std::optional<double>& value);

#endif // TIMEFORMAT_H
