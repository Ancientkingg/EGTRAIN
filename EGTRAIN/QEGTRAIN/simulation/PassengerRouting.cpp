#include "simulation/PassengerRouting.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <utility>

namespace passenger_routing {

namespace {
// The planned time at which the train leaves the stop: its departure, or its arrival when it has none; below 0 for neither.
double plannedLeave(const Stop& stop) {
	return stop.departure >= 0.0 ? stop.departure : stop.arrival;
}

// The planned time at which the train reaches the stop: its arrival, or its departure when it has none; below 0 for neither.
double plannedReach(const Stop& stop) {
	return stop.arrival >= 0.0 ? stop.arrival : stop.departure;
}

// A planned time moved by seconds; a missing time stays missing.
double shifted(double planned, double seconds) {
	return planned >= 0.0 ? planned + seconds : planned;
}

// True when the state has no predicted times, or one of each per stop of the train.
bool fits(const TrainState& state, const TrainTimetable& train) {
	return (state.arrivals.empty() && state.departures.empty())
		|| (state.arrivals.size() == train.stops.size() && state.departures.size() == train.stops.size());
}
} // namespace

double walkingSeconds(int fromPlatformNumber, int toPlatformNumber, double secondsPerPlatform) {
	if (fromPlatformNumber == 0 || toPlatformNumber == 0)
		return 0.0;
	return secondsPerPlatform * std::abs(fromPlatformNumber - toPlatformNumber);
}

double limitedStretchSeconds(double lengthMetres, double speedMs, double limitMs) {
	const double speed = speedMs > 0.0 && std::isfinite(speedMs) ? std::min(speedMs, limitMs) : limitMs;
	return lengthMetres / speed;
}

TrainState predictTrainState(const TrainTimetable& timetable, const TrainProgress& progress, double lateThresholdSeconds) {
	TrainState state;
	state.firstStop = progress.firstStop;
	state.delay = progress.delay;
	state.late = progress.delay > lateThresholdSeconds;
	state.slowed = progress.limited;
	const std::vector<Stop>& stops = timetable.stops;
	const std::size_t count = stops.size();
	const bool predicted = state.late || state.slowed;
	const double shift = predicted ? progress.delay : 0.0;
	const bool standing = progress.standing && progress.firstStop < count;
	// A train that stands at its stop past the time it would leave is boarded there until it leaves.
	if (!predicted && !(standing && stops[progress.firstStop].departure >= 0.0 && stops[progress.firstStop].departure < progress.now))
		return state;
	state.arrivals.resize(count);
	state.departures.resize(count);
	for (std::size_t stop = 0; stop < count; ++stop) {
		state.arrivals[stop] = shifted(stops[stop].arrival, shift);
		state.departures[stop] = shifted(stops[stop].departure, shift);
	}
	const auto hold = [&state, &progress, standing]() {
		if (standing && state.departures[progress.firstStop] >= 0.0)
			state.departures[progress.firstStop] = std::max(state.departures[progress.firstStop], progress.now);
	};
	hold();
	if (!state.slowed)
		return state;
	// The lateness that the train carries from stop to stop: the delay, and what it loses under the limit.
	double lateness = shift;
	for (std::size_t stop = progress.nextStop; stop < count; ++stop) {
		double reached = -1.0;
		if (stop == progress.nextStop) {
			reached = progress.nextArrival;
		} else {
			const std::size_t from = stop - 1;
			const double leave = state.departures[from] >= 0.0 ? state.departures[from] : state.arrivals[from];
			const double run = from < progress.runs.size() ? progress.runs[from] : -1.0;
			if (leave >= 0.0 && leave < progress.limitEnds && run >= 0.0 && plannedLeave(stops[from]) >= 0.0 && plannedReach(stops[stop]) >= 0.0)
				reached = leave + std::max(plannedReach(stops[stop]) - plannedLeave(stops[from]), run);
		}
		if (reached >= 0.0 && plannedReach(stops[stop]) >= 0.0)
			lateness = std::max(lateness, reached - plannedReach(stops[stop]));
		state.arrivals[stop] = shifted(stops[stop].arrival, lateness);
		state.departures[stop] = shifted(stops[stop].departure, lateness);
		if (stop == progress.firstStop)
			hold();
	}
	return state;
}

double Search::arrival(std::size_t station) const {
	return station < best_.size() ? best_[station] : kUnreached;
}

Plan Search::plan(std::size_t station) const {
	Plan result;
	if (station >= best_.size() || best_[station] == kUnreached)
		return result;
	result.arrival = best_[station];
	// The first round that reaches the earliest arrival uses the fewest trains.
	std::size_t round = 0;
	while (round < rounds_.size() && rounds_[round][station].arrival != best_[station])
		++round;
	std::size_t at = station;
	for (; round > 0; --round) {
		const Label& label = rounds_[round][at];
		const TrainTimetable& train = (*trains_)[label.train];
		result.legs.push_back({label.train, label.boardStop, label.alightStop, (*states_)[label.train].departure(train, label.boardStop),
			label.arrival});
		at = static_cast<std::size_t>(train.stops[label.boardStop].station);
	}
	std::reverse(result.legs.begin(), result.legs.end());
	return result;
}

Router::Router(std::vector<TrainTimetable> trains, std::size_t stationCount, double walkingSecondsPerPlatform)
	: trains_(std::move(trains)), stationCount_(stationCount), walkingSecondsPerPlatform_(walkingSecondsPerPlatform), departures_(stationCount) {
	for (std::size_t train = 0; train < trains_.size(); ++train) {
		const std::vector<Stop>& stops = trains_[train].stops;
		// A departure at the last stop leads nowhere.
		for (std::size_t stop = 0; stop + 1 < stops.size(); ++stop) {
			const int station = stops[stop].station;
			if (station >= 0 && static_cast<std::size_t>(station) < stationCount_ && stops[stop].departure >= 0.0)
				departures_[static_cast<std::size_t>(station)].emplace_back(train, stop);
		}
	}
}

Search Router::search(std::size_t origin, double readyTime, int maxTransfers, const std::vector<TrainState>& states,
	int originPlatformNumber) const {
	Search result;
	result.trains_ = &trains_;
	result.states_ = &states;
	result.best_.assign(stationCount_, kUnreached);
	if (origin >= stationCount_ || states.size() != trains_.size())
		return result;
	for (std::size_t train = 0; train < trains_.size(); ++train)
		if (!fits(states[train], trains_[train]))
			return result;

	result.rounds_.emplace_back(stationCount_);
	result.rounds_[0][origin].arrival = readyTime;
	result.rounds_[0][origin].platformNumber = originPlatformNumber;
	result.best_[origin] = readyTime;
	std::vector<std::size_t> marked{origin};
	// The earliest stop where each train can be boarded in the current round; npos when it cannot.
	constexpr std::size_t npos = static_cast<std::size_t>(-1);
	std::vector<std::size_t> boardAt(trains_.size(), npos);
	std::vector<std::size_t> boarded;
	std::vector<char> isMarked(stationCount_, 0);

	const int rounds = std::max(0, maxTransfers) + 1;
	for (int round = 1; round <= rounds && !marked.empty(); ++round) {
		const std::vector<Search::Label>& previous = result.rounds_[static_cast<std::size_t>(round - 1)];
		// Every train that departs from a station reached in the previous round, at the earliest stop where the
		// passenger is on the platform in time.
		for (const std::size_t station : marked) {
			const Search::Label& reached = previous[station];
			for (const auto& [train, stop] : departures_[station]) {
				const TrainState& state = states[train];
				if (stop < state.firstStop)
					continue;
				const double walking = walkingSeconds(reached.platformNumber, trains_[train].stops[stop].platformNumber, walkingSecondsPerPlatform_);
				if (reached.arrival + walking > state.departure(trains_[train], stop))
					continue;
				if (boardAt[train] == npos)
					boarded.push_back(train);
				boardAt[train] = std::min(boardAt[train], stop);
			}
		}

		result.rounds_.emplace_back(stationCount_);
		std::vector<Search::Label>& current = result.rounds_.back();
		marked.clear();
		for (const std::size_t train : boarded) {
			const std::vector<Stop>& stops = trains_[train].stops;
			const TrainState& state = states[train];
			const std::size_t board = boardAt[train];
			for (std::size_t stop = board + 1; stop < stops.size(); ++stop) {
				const Stop& alight = stops[stop];
				if (alight.station < 0 || static_cast<std::size_t>(alight.station) >= stationCount_ || alight.arrival < 0.0)
					continue;
				const std::size_t station = static_cast<std::size_t>(alight.station);
				const double arrival = state.arrival(trains_[train], stop);
				// Equal arrivals keep the earlier round, which uses fewer trains. In the same round the train that leaves
				// earlier wins, and the first one found when they leave together.
				if (arrival > result.best_[station])
					continue;
				if (arrival == result.best_[station]) {
					const Search::Label& held = current[station];
					if (held.arrival != arrival
						|| state.departure(trains_[train], board) >= states[held.train].departure(trains_[held.train], held.boardStop))
						continue;
				}
				result.best_[station] = arrival;
				current[station] = {arrival, alight.platformNumber, train, board, stop};
				if (!isMarked[station]) {
					isMarked[station] = 1;
					marked.push_back(station);
				}
			}
			boardAt[train] = npos;
		}
		boarded.clear();
		for (const std::size_t station : marked)
			isMarked[station] = 0;
	}
	return result;
}

} // namespace passenger_routing
