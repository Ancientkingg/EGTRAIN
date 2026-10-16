#ifndef SCENEFIELDS_H
#define SCENEFIELDS_H

#include "scene/SceneModel.h"

#include <array>

// A physical field of a train unit: its key in rolling_stock.json and its member
// in SceneTrainPhysical.
struct SceneTrainPhysicalField {
	const char* key;
	double SceneTrainPhysical::* member;
};

// The nine physical fields in the order of the legacy vehicle data file, so the
// position of a value in that file is its index here.
inline constexpr std::array<SceneTrainPhysicalField, 9> kSceneTrainPhysicalFields{{
	{"mass_of_traction_unit_kg", &SceneTrainPhysical::mass_of_traction_unit_kg},
	{"mass_of_a_wagon_kg", &SceneTrainPhysical::mass_of_a_wagon_kg},
	{"number_of_wagons", &SceneTrainPhysical::number_of_wagons},
	{"max_speed_ms", &SceneTrainPhysical::max_speed_ms},
	{"max_deceleration_ms2", &SceneTrainPhysical::max_deceleration_ms2},
	{"frontal_area_m2", &SceneTrainPhysical::frontal_area_m2},
	{"resistance_coefficient", &SceneTrainPhysical::resistance_coefficient},
	{"jerk_ms3", &SceneTrainPhysical::jerk_ms3},
	{"length_m", &SceneTrainPhysical::length_m},
}};

#endif // SCENEFIELDS_H
