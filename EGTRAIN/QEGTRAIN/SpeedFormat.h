#ifndef SPEEDFORMAT_H
#define SPEEDFORMAT_H

#include <string>

// Format a speed in km/h as a short label, e.g. "83 km/h". Clamps negatives to 0.
std::string formatSpeedLabel(double kmh);

#endif // SPEEDFORMAT_H
