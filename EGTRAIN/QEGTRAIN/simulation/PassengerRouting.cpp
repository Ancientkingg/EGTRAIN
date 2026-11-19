#include "simulation/PassengerRouting.h"

#include <algorithm>
#include <cstdlib>
#include <utility>

namespace passenger_routing {

double walkingSeconds(int fromPlatformNumber, int toPlatformNumber) {
	if (fromPlatformNumber == 0 || toPlatformNumber == 0)
		return 0.0;
	const int difference = std::abs(fromPlatformNumber - toPlatformNumber);
	return difference <= 1 ? 0.0 : kWalkingSecondsPerPlatform * difference;
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
		const double delay = (*states_)[label.train].delay;
		result.legs.push_back({label.train, label.boardStop, label.alightStop, train.stops[label.boardStop].departure + delay,
			label.arrival});
		at = static_cast<std::size_t>(train.stops[label.boardStop].station);
	}
	std::reverse(result.legs.begin(), result.legs.end());
	return result;
}

Router::Router(std::vector<TrainTimetable> trains, std::size_t stationCount)
	: trains_(std::move(trains)), stationCount_(stationCount), departures_(stationCount) {
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

Search Router::search(std::size_t origin, double readyTime, int maxTransfers, const std::vector<TrainState>& states) const {
	Search result;
	result.trains_ = &trains_;
	result.states_ = &states;
	result.best_.assign(stationCount_, kUnreached);
	if (origin >= stationCount_ || states.size() != trains_.size())
		return result;

	result.rounds_.emplace_back(stationCount_);
	result.rounds_[0][origin].arrival = readyTime;
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
				const Stop& departure = trains_[train].stops[stop];
				const double walking = round == 1 ? 0.0 : walkingSeconds(reached.platformNumber, departure.platformNumber);
				if (reached.arrival + walking > departure.departure + state.delay)
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
			const double delay = states[train].delay;
			const std::size_t board = boardAt[train];
			for (std::size_t stop = board + 1; stop < stops.size(); ++stop) {
				const Stop& alight = stops[stop];
				if (alight.station < 0 || static_cast<std::size_t>(alight.station) >= stationCount_ || alight.arrival < 0.0)
					continue;
				const std::size_t station = static_cast<std::size_t>(alight.station);
				const double arrival = alight.arrival + delay;
				// Equal arrivals keep the earlier round, which uses fewer trains.
				if (arrival >= result.best_[station])
					continue;
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
