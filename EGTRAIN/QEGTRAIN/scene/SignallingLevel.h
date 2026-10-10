#pragma once

// The signalling levels. The numbers are the values stored in signalling.json and in Section::SignallingLevel.
// Their display labels are in scene/SignallingLevelNames.h.
enum class SignallingLevel : int {
	Atb = 0,
	EtcsLevel1 = 1,
	EtcsLevel2 = 2,
	EtcsLevel3 = 3,
	VirtualCoupling = 4,
	Bacc = 5
};

// The level of a runtime section that no signalling area covers. The editor also stores it as the level
// of an area whose signalling system is not chosen yet.
inline constexpr int kSignallingLevelUnset = -99999999;

// The number stored for a level.
constexpr int levelValue(SignallingLevel level) {
	return static_cast<int>(level);
}

// Whether a stored number is a signalling level. kSignallingLevelUnset is not a valid level.
constexpr bool isValidSignallingLevel(int level) {
	return level >= levelValue(SignallingLevel::Atb) && level <= levelValue(SignallingLevel::Bacc);
}
