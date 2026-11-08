#pragma once

#include "simulation/GuiSimulationSnapshot.h"

#include <array>
#include <cstddef>
#include <iomanip>
#include <sstream>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

// Compares two GuiSimulationSnapshot objects field by field, doubles with ==. Every field of every type is bound by name
// in fields(): a field added to one of the types stops the build here. Compare it, and name it in the matching
// snapshotFieldNames().

/// The fields of an occupied arc, in declaration order.
inline auto fields(const GuiOccupiedArc& v) {
	const auto& [trackId, startX] = v;
	return std::make_tuple(trackId, startX);
}

/// The fields of a list of occupied arcs, in order.
inline std::vector<std::tuple<int, double>> arcFields(const std::vector<GuiOccupiedArc>& arcs) {
	std::vector<std::tuple<int, double>> result;
	for (const GuiOccupiedArc& arc : arcs) result.push_back(fields(arc));
	return result;
}

/// The fields of a train, in declaration order.
inline auto fields(const GuiTrainState& v) {
	const auto& [index, id, type, description, operatingCode, serviceId, occurrence, routeIndex, reversedDirection, wagonCount, length,
		departureTime, outOfSimulation, routeAxisPosition, speedKmh, currentOnboardPassengers, maxOnboardPassengers, wagonHeadPositions,
		wagonTailPositions, occupiedArcs] = v;
	return std::make_tuple(index, id, type, description, operatingCode, serviceId, occurrence, routeIndex, reversedDirection, wagonCount,
		length, departureTime, outOfSimulation, routeAxisPosition, speedKmh, currentOnboardPassengers, maxOnboardPassengers,
		wagonHeadPositions, wagonTailPositions, arcFields(occupiedArcs));
}

/// The fields of a signal state, in declaration order.
inline auto fields(const GuiSignalState& v) {
	const auto& [sectionId, code, reversedDirection, level, failed] = v;
	return std::make_tuple(sectionId, code, reversedDirection, level, failed);
}

/// The fields of a section state, in declaration order.
inline auto fields(const GuiSectionState& v) {
	const auto& [sectionId, prepared, blocked] = v;
	return std::make_tuple(sectionId, prepared, blocked);
}

/// The fields of a platform, in declaration order.
inline auto fields(const GuiPlatformState& v) {
	const auto& [stationId, platformId, maxVolume, passengerIds] = v;
	return std::make_tuple(stationId, platformId, maxVolume, passengerIds);
}

/// The fields of a passenger, in declaration order.
inline auto fields(const GuiPassengerState& v) {
	const auto& [id, status, waitingPlatform, nextTrain, nextDestination] = v;
	return std::make_tuple(id, status, waitingPlatform, nextTrain, nextDestination);
}

/// The fields of a virtual coupling message, in declaration order.
inline auto fields(const GuiVirtualCouplingState& v) {
	const auto& [messageStep, trainDescription, message] = v;
	return std::make_tuple(messageStep, trainDescription, message);
}

/// The two lists have the same length and equal fields in every element.
template <class T> bool sameList(const std::vector<T>& a, const std::vector<T>& b) {
	if (a.size() != b.size())
		return false;
	for (std::size_t i = 0; i < a.size(); ++i)
		if (fields(a[i]) != fields(b[i]))
			return false;
	return true;
}

/// Every field of the two snapshots is equal.
inline bool sameSnapshot(const GuiSimulationSnapshot& a, const GuiSimulationSnapshot& b) {
	const auto& [step, totalTimesteps, trains, signalStates, sectionStates, platforms, passengers, messages] = a;
	return step == b.timestep && totalTimesteps == b.totalTimesteps && sameList(trains, b.trains)
		&& sameList(signalStates, b.signalStates) && sameList(sectionStates, b.sectionStates) && sameList(platforms, b.platforms)
		&& sameList(passengers, b.passengers) && sameList(messages, b.virtualCouplingMessages);
}

namespace snapshot_compare_detail {

// The names of the fields of fields(), for the message of firstSnapshotDifference. A count that does not match the tuple
// stops the build.
inline const std::array<const char*, 2> arcNames{"trackId", "startX"};
inline const std::array<const char*, 20> trainNames{"index", "id", "type", "description", "operatingCode", "serviceId", "occurrence",
	"routeIndex", "reversedDirection", "wagonCount", "length", "departureTime", "outOfSimulation", "routeAxisPosition", "speedKmh",
	"currentOnboardPassengers", "maxOnboardPassengers", "wagonHeadPositions", "wagonTailPositions", "occupiedArcs"};
inline const std::array<const char*, 5> signalNames{"sectionId", "code", "reversedDirection", "level", "failed"};
inline const std::array<const char*, 3> sectionNames{"sectionId", "prepared", "blocked"};
inline const std::array<const char*, 4> platformNames{"stationId", "platformId", "maxVolume", "passengerIds"};
inline const std::array<const char*, 5> passengerNames{"id", "status", "waitingPlatform", "nextTrain", "nextDestination"};
inline const std::array<const char*, 3> messageNames{"timestep", "trainDescription", "message"};

template <class T> std::string show(const T& value) {
	std::ostringstream text;
	text << std::setprecision(17) << std::boolalpha;
	if constexpr (std::is_same_v<T, std::string>)
		text << '"' << value << '"';
	else
		text << value;
	return text.str();
}

// Each returns true and sets `out` when the two values differ. Declared first: the overloads call each other.
template <class T> bool differs(const std::string& path, const T& a, const T& b, std::string& out);
template <class E> bool differs(const std::string& path, const std::vector<E>& a, const std::vector<E>& b, std::string& out);
inline bool differs(const std::string& path, const std::tuple<int, double>& a, const std::tuple<int, double>& b, std::string& out);
inline bool differs(const std::string& path, const GuiTrainState& a, const GuiTrainState& b, std::string& out);
inline bool differs(const std::string& path, const GuiSignalState& a, const GuiSignalState& b, std::string& out);
inline bool differs(const std::string& path, const GuiSectionState& a, const GuiSectionState& b, std::string& out);
inline bool differs(const std::string& path, const GuiPlatformState& a, const GuiPlatformState& b, std::string& out);
inline bool differs(const std::string& path, const GuiPassengerState& a, const GuiPassengerState& b, std::string& out);
inline bool differs(const std::string& path, const GuiVirtualCouplingState& a, const GuiVirtualCouplingState& b, std::string& out);

template <class T> bool differs(const std::string& path, const T& a, const T& b, std::string& out) {
	if (a == b)
		return false;
	out = path + ": " + show(a) + " vs " + show(b);
	return true;
}

template <class E> bool differs(const std::string& path, const std::vector<E>& a, const std::vector<E>& b, std::string& out) {
	if (a.size() != b.size()) {
		out = path + ": " + std::to_string(a.size()) + " vs " + std::to_string(b.size()) + " entries";
		return true;
	}
	for (std::size_t i = 0; i < a.size(); ++i)
		if (differs(path + "[" + std::to_string(i) + "]", a[i], b[i], out))
			return true;
	return false;
}

template <class Tuple, std::size_t N, std::size_t... I>
bool differsTuple(const std::string& path, const std::array<const char*, N>& names, const Tuple& a, const Tuple& b, std::string& out,
	std::index_sequence<I...>) {
	static_assert(std::tuple_size_v<Tuple> == N, "a field of a snapshot type is not named in snapshot_compare_detail");
	return (differs(path + "." + names[I], std::get<I>(a), std::get<I>(b), out) || ...);
}

template <class Tuple, std::size_t N>
bool differsFields(const std::string& path, const std::array<const char*, N>& names, const Tuple& a, const Tuple& b, std::string& out) {
	return differsTuple(path, names, a, b, out, std::make_index_sequence<N>());
}

inline bool differs(const std::string& path, const std::tuple<int, double>& a, const std::tuple<int, double>& b, std::string& out) {
	return differsFields(path, arcNames, a, b, out);
}
inline bool differs(const std::string& path, const GuiTrainState& a, const GuiTrainState& b, std::string& out) {
	return differsFields(path, trainNames, fields(a), fields(b), out);
}
inline bool differs(const std::string& path, const GuiSignalState& a, const GuiSignalState& b, std::string& out) {
	return differsFields(path, signalNames, fields(a), fields(b), out);
}
inline bool differs(const std::string& path, const GuiSectionState& a, const GuiSectionState& b, std::string& out) {
	return differsFields(path, sectionNames, fields(a), fields(b), out);
}
inline bool differs(const std::string& path, const GuiPlatformState& a, const GuiPlatformState& b, std::string& out) {
	return differsFields(path, platformNames, fields(a), fields(b), out);
}
inline bool differs(const std::string& path, const GuiPassengerState& a, const GuiPassengerState& b, std::string& out) {
	return differsFields(path, passengerNames, fields(a), fields(b), out);
}
inline bool differs(const std::string& path, const GuiVirtualCouplingState& a, const GuiVirtualCouplingState& b, std::string& out) {
	return differsFields(path, messageNames, fields(a), fields(b), out);
}

}

/// Empty when sameSnapshot(a, b). Otherwise the path of the first field that differs and both values, such as
/// "trains[12].wagonHeadPositions[3]: 10.5 vs 10.499" or "signalStates: 1331 vs 1330 entries". Doubles are printed with
/// 17 significant digits.
inline std::string firstSnapshotDifference(const GuiSimulationSnapshot& a, const GuiSimulationSnapshot& b) {
	if (sameSnapshot(a, b))
		return {};
	using snapshot_compare_detail::differs;
	std::string out;
	if (differs("timestep", a.timestep, b.timestep, out) || differs("totalTimesteps", a.totalTimesteps, b.totalTimesteps, out)
		|| differs("trains", a.trains, b.trains, out) || differs("signalStates", a.signalStates, b.signalStates, out)
		|| differs("sectionStates", a.sectionStates, b.sectionStates, out) || differs("platforms", a.platforms, b.platforms, out)
		|| differs("passengers", a.passengers, b.passengers, out)
		|| differs("virtualCouplingMessages", a.virtualCouplingMessages, b.virtualCouplingMessages, out))
		return out;
	return "snapshots differ";
}
