#include "util/TimeFormat.h"
#include <cstdio>

std::string formatSimTime(long long simSeconds, long long baseOffsetSeconds) {
    long long total = simSeconds + baseOffsetSeconds;
    if (total < 0) total = 0;
    total %= 86400; // wrap to a single day
    long long h = total / 3600;
    long long m = (total % 3600) / 60;
    long long s = total % 60;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02lld:%02lld:%02lld", h, m, s);
    return std::string(buf);
}

long long parseClockToSeconds(const std::string& hhmm) {
    int h = 0, m = 0;
    char extra = 0;
    // require exactly HH:MM with no trailing characters
    if (std::sscanf(hhmm.c_str(), "%d:%d%c", &h, &m, &extra) != 2)
        return -1;
    if (h < 0 || h > 23 || m < 0 || m > 59)
        return -1;
    return static_cast<long long>(h) * 3600 + static_cast<long long>(m) * 60;
}
