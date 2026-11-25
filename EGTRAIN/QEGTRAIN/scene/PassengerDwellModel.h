#ifndef PASSENGERDWELLMODEL_H
#define PASSENGERDWELLMODEL_H

#include <array>
#include <cstddef>

/// The number of coefficients (beta0 to beta7) of the passenger-dependent dwell model of Train::computePaxDependentDwellTimeAtStations.
inline constexpr std::size_t kPassengerDwellCoefficientCount = 8;

/// The largest value of a coefficient in passenger_dwell_coefficients. The run holds the values as floats.
inline constexpr double kMaxPassengerDwellCoefficient = 1000000.0;

/// The coefficients of the dwell model in model order, from the calibrated Gibson dwell model of the legacy Paimpol case
/// (10 boarding and 10 alighting passengers per door give 40 s), rescaled to per-passenger values, that a scene without
/// passenger_dwell_coefficients uses:
///  beta0 fixed time of a stop in s,
///  beta1 added to the dwell of the stop when the platform is crowded,
///  beta2 time per boarding passenger at a door in s,
///  beta3 added to the boarding time of a door when the platform is crowded,
///  beta4 added to the boarding time of a door when the car has many boarding passengers,
///  beta5 time per alighting passenger at a door in s,
///  beta6 rate at which that time falls with the number of passengers alighting at the door,
///  beta7 added to the alighting time of a door when the train is crowded on board.
inline constexpr std::array<double, kPassengerDwellCoefficientCount> kDefaultPassengerDwellCoefficients{
	7.0, 0.32, 1.823, 0.564, 4.838, 2.224, 0.04, 0.562};
/// The platform occupancy rate above which the platform counts as crowded (passenger_platform_crowding_threshold).
inline constexpr double kDefaultPassengerPlatformCrowdingThreshold = 0.65;
/// The on-board occupancy rate above which the train counts as crowded (passenger_onboard_crowding_threshold).
inline constexpr double kDefaultPassengerOnboardCrowdingThreshold = 0.7;
/// The number of boarding passengers of a car above which the car counts as crowded (passenger_car_boarding_threshold).
inline constexpr int kDefaultPassengerCarBoardingThreshold = 15;

#endif // PASSENGERDWELLMODEL_H
