#pragma once

#include <string>

/// The signalling levels. The numbers are the values stored in signalling.json and in Section::SignallingLevel.
/// Their display labels are in scene/SignallingLevelNames.h.
enum class SignallingLevel : int {
	Atb = 0,			 ///< Level 0, fixed block, labelled ATB.
	EtcsLevel1 = 1,		 ///< Level 1, fixed block, labelled ETCS Level 1.
	EtcsLevel2 = 2,		 ///< Level 2, fixed block, labelled ETCS Level 2.
	EtcsLevel3 = 3,		 ///< Level 3, moving block, labelled ETCS Level 3.
	VirtualCoupling = 4, ///< Level 4, moving block in which a train can follow the train ahead.
	Bacc = 5			 ///< Level 5, fixed block, labelled BACC.
};

/// The level of a runtime section that no signalling area covers. The editor also stores it as the level
/// of an area whose signalling system is not chosen yet.
inline constexpr int kSignallingLevelUnset = -99999999;

/// The number stored for a level.
constexpr int levelValue(SignallingLevel level) {
	return static_cast<int>(level);
}

/// Whether a stored number is a signalling level. kSignallingLevelUnset is not a valid level.
constexpr bool isValidSignallingLevel(int level) {
	return level >= levelValue(SignallingLevel::Atb) && level <= levelValue(SignallingLevel::Bacc);
}

/// Whether a stored number is a moving-block level. There the movement authority of a train ends behind the
/// train ahead, not at a signal.
constexpr bool isMovingBlockLevel(int level) {
	return level == levelValue(SignallingLevel::EtcsLevel3) || level == levelValue(SignallingLevel::VirtualCoupling);
}

/// Whether a stored number is a fixed-block level, where signal aspects separate the trains.
constexpr bool isFixedBlockLevel(int level) {
	return isValidSignallingLevel(level) && !isMovingBlockLevel(level);
}

/// How a run changes the moving-block levels of the signalling areas of its scene (--signalling-level-override). The
/// scene keeps its own levels; only the run that is built from it gets the other level.
enum class SignallingLevelOverride : int {
	None,	   ///< Every area keeps its level.
	Level3To4, ///< Every area of level 3 runs at level 4.
	Level4To3  ///< Every area of level 4 runs at level 3.
};

/// The level that a signalling area of `level` gets under `change`.
constexpr int overriddenSignallingLevel(int level, SignallingLevelOverride change) {
	if (change == SignallingLevelOverride::Level3To4 && level == levelValue(SignallingLevel::EtcsLevel3))
		return levelValue(SignallingLevel::VirtualCoupling);
	if (change == SignallingLevelOverride::Level4To3 && level == levelValue(SignallingLevel::VirtualCoupling))
		return levelValue(SignallingLevel::EtcsLevel3);
	return level;
}

/// The text of `change` on the command line and in the provenance of a run: "3to4", "4to3", or "none".
constexpr const char* signallingLevelOverrideName(SignallingLevelOverride change) {
	switch (change) {
		case SignallingLevelOverride::Level3To4:
			return "3to4";
		case SignallingLevelOverride::Level4To3:
			return "4to3";
		case SignallingLevelOverride::None:
			break;
	}
	return "none";
}

/// Reads the value of --signalling-level-override: "3to4" or "4to3". Returns false and leaves `change` as it is for any
/// other text, "none" included.
inline bool parseSignallingLevelOverride(const std::string& text, SignallingLevelOverride& change) {
	for (const SignallingLevelOverride candidate : {SignallingLevelOverride::Level3To4, SignallingLevelOverride::Level4To3})
		if (text == signallingLevelOverrideName(candidate)) {
			change = candidate;
			return true;
		}
	return false;
}
