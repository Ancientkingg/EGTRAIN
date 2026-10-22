#ifndef SPEEDFORMAT_H
#define SPEEDFORMAT_H

#include <string>

/// \file
/// Text form of a speed.

/// Formats a speed \p kmh in km/h as a label such as "83 km/h", rounded to a whole number. A
/// negative speed gives "0 km/h".
std::string formatSpeedLabel(double kmh);

#endif // SPEEDFORMAT_H
