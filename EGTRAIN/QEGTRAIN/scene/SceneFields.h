#ifndef SCENEFIELDS_H
#define SCENEFIELDS_H

#include "scene/SceneModel.h"

#include <array>

// The range of a number of a train unit. The editor refuses a number outside [min, max]. Validation warns about a
// number outside [plausibleMin, plausibleMax], which lies inside the first range. Both are guards against input that
// no rail vehicle has, not data of a vehicle.
struct SceneValueRange {
	double min;
	double max;
	double plausibleMin;
	double plausibleMax;
};

// A physical field of a train unit: its key in rolling_stock.json, its member
// in SceneTrainPhysical and its range.
struct SceneTrainPhysicalField {
	const char* key;
	double SceneTrainPhysical::* member;
	SceneValueRange range;
};

// The nine physical fields in the order of the legacy vehicle data file, so the
// position of a value in that file is its index here.
inline constexpr std::array<SceneTrainPhysicalField, 9> kSceneTrainPhysicalFields{{
	// locomotives weigh up to about 250 t
	{"mass_of_traction_unit_kg", &SceneTrainPhysical::mass_of_traction_unit_kg, {0.0, 5e6, 0.0, 5e5}},
	// a loaded freight wagon weighs about 100 t
	{"mass_of_a_wagon_kg", &SceneTrainPhysical::mass_of_a_wagon_kg, {0.0, 1e6, 0.0, 2e5}},
	// the longest freight trains have about 200 wagons
	{"number_of_wagons", &SceneTrainPhysical::number_of_wagons, {0.0, 1000.0, 0.0, 200.0}},
	// 120 m/s is 432 km/h; trains on rails have run 575 km/h
	{"max_speed_ms", &SceneTrainPhysical::max_speed_ms, {0.0, 200.0, 0.0, 120.0}},
	// the emergency braking of a train stays below about 3 m/s²
	{"max_deceleration_ms2", &SceneTrainPhysical::max_deceleration_ms2, {0.0, 10.0, 0.0, 3.0}},
	// the cross-section of a train is about 14 m² at most
	{"frontal_area_m2", &SceneTrainPhysical::frontal_area_m2, {0.0, 1000.0, 0.0, 50.0}},
	// the factor of the speed term of the wagon resistance, 0.004 in the committed cases
	{"resistance_coefficient", &SceneTrainPhysical::resistance_coefficient, {0.0, 1.0, 0.0, 0.1}},
	// passenger comfort asks for about 1 m/s³
	{"jerk_ms3", &SceneTrainPhysical::jerk_ms3, {0.0, 100.0, 0.0, 10.0}},
	// a freight train of 100 wagons is about 1.5 km long
	{"length_m", &SceneTrainPhysical::length_m, {0.0, 3000.0, 0.0, 1500.0}},
}};

// A column of a row of the traction curve: its name in rolling_stock.json and its range. The force in N at speed v is
// C0 + C1 v + C2 v².
struct SceneTractionColumn {
	const char* key;
	SceneValueRange range;
};

inline constexpr std::array<SceneTractionColumn, 5> kSceneTractionColumns{{
	// the speeds are limited like the maximum speed
	{"v_lower", {0.0, 200.0, 0.0, 120.0}},
	{"v_upper", {0.0, 200.0, 0.0, 120.0}},
	// the coefficients of a force in N; a locomotive starts with about 0.5 MN
	{"c0", {-1e9, 1e9, -5e6, 5e6}},
	{"c1", {-1e9, 1e9, -1e6, 1e6}},
	{"c2", {-1e9, 1e9, -5e6, 5e6}},
}};

#endif // SCENEFIELDS_H
