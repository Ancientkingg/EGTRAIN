#pragma once

#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

/// \file
/// Conversion of a value to text.

/// The text that operator<< writes for \p t to a std::stringstream.
template <typename T>
inline std::string tostring(T t) {
	std::stringstream ss;
	ss << t;
	return ss.str();
}
