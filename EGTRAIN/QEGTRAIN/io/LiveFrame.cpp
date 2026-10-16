#include "io/LiveFrame.h"

#include <algorithm>
#include <cmath>

using nlohmann::json;

namespace {
// Train::trainXPosition returns this when the head lies in no block section of the route.
constexpr double kNoXPosition = -1.0;
constexpr double kKmhPerMetrePerSecond = 3.6;

// JSON has no NaN and no infinity.
json number(double value) {
	return std::isfinite(value) ? json(value) : json(nullptr);
}

json numberIf(bool known, double value) {
	return known ? number(value) : json(nullptr);
}

json frameHead(const LiveRunInfo& run, std::uint64_t seq) {
	json frame = json::object();
	frame["v"] = kLiveContractVersion;
	frame["run_id"] = run.runId;
	frame["seq"] = seq;
	return frame;
}

json trainIdentity(const GuiTrainState& train) {
	json identity = json::object();
	identity["service_id"] = train.serviceId;
	identity["occurrence"] = train.occurrence;
	identity["operating_code"] = train.operatingCode;
	return identity;
}

json trainState(const GuiTrainState& train, int step) {
	const bool located = guiReplayTrainHasPosition(train, step);
	json state = trainIdentity(train);
	state["in_network"] = !train.outOfSimulation && step >= train.departureTime;
	state["direction"] = train.reversedDirection ? "reversed" : "forward";
	state["route_pos_m"] = numberIf(located, train.routeAxisPosition);
	const double x = located ? train.wagonHeadPositions.front() : kNoXPosition;
	state["x_km"] = numberIf(x != kNoXPosition, x);
	state["speed_ms"] = numberIf(located, train.speedKmh / kKmhPerMetrePerSecond);
	state["onboard"] = train.currentOnboardPassengers;
	return state;
}

json signalState(const GuiSignalState& signal) {
	const bool leveled = guiSignalHasLevel(signal.level);
	json state = json::object();
	state["section_id"] = signal.sectionId;
	state["code"] = leveled ? json(signal.code) : json(nullptr);
	state["level"] = leveled ? json(signal.level) : json(nullptr);
	state["failed"] = signal.failed;
	state["reversed"] = signal.reversedDirection;
	return state;
}
} // namespace

json makeHelloFrame(const LiveRunInfo& run, std::uint64_t seq, const GuiSimulationSnapshot& snapshot) {
	json frame = frameHead(run, seq);
	frame["scene_name"] = run.sceneName;
	frame["base_time_s"] = number(run.baseTimeSeconds);
	frame["horizon_steps"] = run.horizonSteps;
	frame["step_s"] = number(run.stepSeconds);
	json units = json::object();
	units["distance"] = "m";
	units["time"] = "s";
	units["speed"] = "m/s";
	units["x"] = "km";
	frame["units"] = units;
	json trains = json::array();
	for (const GuiTrainState& train : snapshot.trains) {
		json entry = trainIdentity(train);
		entry["route_index"] = train.routeIndex;
		trains.push_back(std::move(entry));
	}
	frame["trains"] = std::move(trains);
	return frame;
}

json makeStateFrame(const LiveRunInfo& run, std::uint64_t seq, const GuiSimulationSnapshot& snapshot, bool includeSignals) {
	json frame = frameHead(run, seq);
	const double simulationTime = snapshot.timestep * run.stepSeconds;
	frame["step"] = snapshot.timestep;
	frame["sim_time_s"] = number(simulationTime);
	frame["clock_s"] = number(run.baseTimeSeconds + simulationTime);
	json trains = json::array();
	for (const GuiTrainState& train : snapshot.trains) trains.push_back(trainState(train, snapshot.timestep));
	frame["trains"] = std::move(trains);
	if (includeSignals) {
		json entries = json::array();
		for (const GuiSignalState& signal : snapshot.signalStates) entries.push_back(signalState(signal));
		frame["signals"] = std::move(entries);
	}
	return frame;
}

json makeEventFrame(const LiveRunInfo& run, std::uint64_t seq, LiveEvent event, int step) {
	json frame = frameHead(run, seq);
	frame["kind"] = event == LiveEvent::StepsFinished ? "steps_finished" : "run_finished";
	frame["step"] = step;
	if (event != LiveEvent::StepsFinished)
		frame["status"] = event == LiveEvent::RunCancelled ? "cancelled" : "completed";
	return frame;
}

bool liveSignalsChanged(const std::vector<GuiSignalState>& previous, const std::vector<GuiSignalState>& current) {
	return !std::equal(previous.begin(), previous.end(), current.begin(), current.end(), [](const GuiSignalState& a, const GuiSignalState& b) {
		return a.sectionId == b.sectionId && a.reversedDirection == b.reversedDirection && guiSignalDisplayCode(a) == guiSignalDisplayCode(b)
			&& a.level == b.level && a.failed == b.failed;
	});
}
