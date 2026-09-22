#ifndef TIMEFORMAT_H
#define TIMEFORMAT_H

#include <string>
#include <optional>

// Format simulation time as clock time of day "HH:MM:SS".
// simSeconds: seconds elapsed since simulation start.
// baseOffsetSeconds: seconds since midnight for the simulation start time.
// Wraps at 24h. Negative results clamp to zero.
std::string formatSimTime(long long simSeconds, long long baseOffsetSeconds = 0);

// Parse "HH:MM" into seconds since midnight. Returns -1 on malformed input.
long long parseClockToSeconds(const std::string& hhmm);

// Planned offsets remain optional doubles. Clock days are explicit, relative to base.
std::string formatPlannedTime(std::optional<double> value, bool clock, long long baseOffsetSeconds);
// On failure value is unchanged; an unchanged representation retains its exact stored value.
bool parsePlannedTime(const std::string& text, bool clock, long long baseOffsetSeconds,
                     std::optional<double>& value);

#endif // TIMEFORMAT_H
