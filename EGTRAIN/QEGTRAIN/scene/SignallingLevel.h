#pragma once

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
