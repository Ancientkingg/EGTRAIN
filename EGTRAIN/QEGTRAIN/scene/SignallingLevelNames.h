#ifndef SCENE_SIGNALLING_LEVEL_NAMES_H
#define SCENE_SIGNALLING_LEVEL_NAMES_H

#include "scene/SignallingLevel.h"

#include <string>

// The names follow the routine names in the simulation code; they are labels, not claims about real systems.
// The level numbers are defined in scene/SignallingLevel.h; this header holds the text shown for them.

// The label of a signalling level: its number and name, so the stored number stays readable.
inline std::string signallingLevelName(int level) {
	switch (level) {
		case kSignallingLevelUnset: return "No signalling";
		case levelValue(SignallingLevel::Atb): return "0 ATB fixed block";
		case levelValue(SignallingLevel::EtcsLevel1): return "1 ETCS Level 1 fixed block";
		case levelValue(SignallingLevel::EtcsLevel2): return "2 ETCS Level 2 fixed block";
		case levelValue(SignallingLevel::EtcsLevel3): return "3 ETCS Level 3 moving block";
		case levelValue(SignallingLevel::VirtualCoupling): return "4 Virtual coupling";
		case levelValue(SignallingLevel::Bacc): return "5 BACC track circuits";
		default: return "Invalid level " + std::to_string(level);
	}
}

// One line that says what the level does in the simulation, for tooltips.
inline std::string signallingLevelDescription(int level) {
	switch (level) {
		case kSignallingLevelUnset: return "Trains in this area are not separated.";
		case levelValue(SignallingLevel::Atb): return "Fixed block; the block before an occupied one is limited to 40 km/h.";
		case levelValue(SignallingLevel::EtcsLevel1): return "Fixed block; moves trains like level 2; no blocking times.";
		case levelValue(SignallingLevel::EtcsLevel2): return "Fixed block; moves trains like level 1; blocking times computed.";
		case levelValue(SignallingLevel::EtcsLevel3): return "Movement authority ends 50 m behind the train ahead.";
		case levelValue(SignallingLevel::VirtualCoupling): return "Moving block; a train can follow the train ahead at its speed.";
		case levelValue(SignallingLevel::Bacc): return "Fixed block; one extra empty block (double red) before an occupied one.";
		default: return "Valid levels are 0 to 5.";
	}
}

#endif // SCENE_SIGNALLING_LEVEL_NAMES_H
