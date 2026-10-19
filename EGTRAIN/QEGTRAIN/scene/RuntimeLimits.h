#ifndef RUNTIME_LIMITS_H
#define RUNTIME_LIMITS_H

#include <cstddef>

/// What a scene must fit into: the scene validator and the builders check against the same values.
namespace RuntimeLimits {

inline constexpr int kMaxExpandedTrains = 700; ///< Train occurrences in one run, over all services.
inline constexpr int kMaxTimetableStops = 40;  ///< Stops of one service.

// Capacities of the native runtime arrays.
inline constexpr std::size_t kNativeMaxTracks = 268;		///< Tracks of a scene.
inline constexpr std::size_t kNativeMaxConnections = 708;	///< Connections of a scene.
inline constexpr std::size_t kNativeMaxStations = 95;		///< Stations of a scene.
inline constexpr std::size_t kNativeMaxRouteBlocks = 600;	///< Section references of one route.
inline constexpr std::size_t kNativeMaxNodeConnections = 6; ///< Connection ends at one node.
inline constexpr std::size_t kNativeMaxDependencies = 10;	///< Switch sections and block dependencies of one section.
inline constexpr std::size_t kNativeMaxSectionArcs = 20;	///< Arcs of one runtime section.
/// Two kilometre positions that differ by no more than this many kilometres are the same position.
inline constexpr double kNativeCoordinateTolerance = 1e-8;

} // namespace RuntimeLimits

#endif // RUNTIME_LIMITS_H
