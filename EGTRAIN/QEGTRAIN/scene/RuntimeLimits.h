#ifndef RUNTIME_LIMITS_H
#define RUNTIME_LIMITS_H

#include <cstddef>

// What a scene must fit into: the scene validator and the builders check against the same values.
namespace RuntimeLimits {

inline constexpr int kMaxExpandedTrains = 700;
inline constexpr int kMaxTimetableStops = 40;

// Capacities of the native runtime arrays.
inline constexpr std::size_t kNativeMaxTracks = 268;
inline constexpr std::size_t kNativeMaxConnections = 708;
inline constexpr std::size_t kNativeMaxStations = 95;
inline constexpr std::size_t kNativeMaxRouteBlocks = 600;
inline constexpr std::size_t kNativeMaxNodeConnections = 6;
inline constexpr std::size_t kNativeMaxDependencies = 10;
inline constexpr std::size_t kNativeMaxSectionArcs = 20;
// Two kilometre positions that differ by no more than this are the same position.
inline constexpr double kNativeCoordinateTolerance = 1e-8;

} // namespace RuntimeLimits

#endif // RUNTIME_LIMITS_H
