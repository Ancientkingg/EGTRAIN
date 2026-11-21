#pragma once

#include <string_view>

/// The text of an absent ID or name in the simulation and its output files: no next section, no leading train, no station.
inline constexpr char kNoneId[] = "None";

/// True when an ID or name field holds kNoneId. An empty field is not absent here.
inline bool isNoneId(std::string_view id) {
	return id == kNoneId;
}
