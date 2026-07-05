#include "util/SpeedFormat.h"
#include <cmath>
#include <cstdio>

std::string formatSpeedLabel(double kmh) {
    if (kmh < 0) kmh = 0;
    int rounded = static_cast<int>(std::lround(kmh));
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%d km/h", rounded);
    return std::string(buf);
}
