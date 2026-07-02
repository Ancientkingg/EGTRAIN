#ifndef TIMEFORMAT_H
#define TIMEFORMAT_H

#include <string>

// Format simulation time as clock time of day "HH:MM:SS".
// simSeconds: seconds elapsed since simulation start.
// baseOffsetSeconds: seconds since midnight for the simulation start time.
// Wraps at 24h. Negative results clamp to zero.
std::string formatSimTime(long long simSeconds, long long baseOffsetSeconds = 0);

// Parse "HH:MM" into seconds since midnight. Returns -1 on malformed input.
long long parseClockToSeconds(const std::string& hhmm);

#endif // TIMEFORMAT_H
