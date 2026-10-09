#ifndef SCENE_SIGNALLING_LEVEL_NAMES_H
#define SCENE_SIGNALLING_LEVEL_NAMES_H

#include <string>

// The names follow the routine names in the simulation code; they are labels, not claims about real systems.

// The level of a runtime section that no signalling area covers.
constexpr int kSignallingLevelUnset = -99999999;

// The label of a signalling level: its number and name, so the stored number stays readable.
inline std::string signallingLevelName(int level) {
	switch (level) {
		case kSignallingLevelUnset: return "No signalling";
		case 0: return "0 ATB fixed block";
		case 1: return "1 ETCS Level 1 fixed block";
		case 2: return "2 ETCS Level 2 fixed block";
		case 3: return "3 ETCS Level 3 moving block";
		case 4: return "4 Virtual coupling";
		case 5: return "5 BACC track circuits";
		default: return "Invalid level " + std::to_string(level);
	}
}

// One line that says what the level does in the simulation, for tooltips.
inline std::string signallingLevelDescription(int level) {
	switch (level) {
		case kSignallingLevelUnset: return "Trains in this area are not separated.";
		case 0: return "Fixed block; the block before an occupied one is limited to 40 km/h.";
		case 1: return "Fixed block; moves trains like level 2; no blocking times.";
		case 2: return "Fixed block; moves trains like level 1; blocking times computed.";
		case 3: return "Movement authority ends 50 m behind the train ahead.";
		case 4: return "Moving block; a train can follow the train ahead at its speed.";
		case 5: return "Fixed block; one extra empty block (double red) before an occupied one.";
		default: return "Valid levels are 0 to 5.";
	}
}

#endif // SCENE_SIGNALLING_LEVEL_NAMES_H
