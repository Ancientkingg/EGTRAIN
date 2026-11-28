#include "simulation/PassengerJourneys.h"
#include "simulation/SimulationRun.h"
#include "util/Log.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <limits>

using namespace std;
using passenger_routing::Leg;
using passenger_routing::Plan;
using passenger_routing::TrainState;

namespace {
int stopCount(const Train& train) {
	return max(0, min(train.numStations, static_cast<int>(train.Stations.size())));
}

// The stops that the train still serves, from the first of its timetable: all of them, or those up to the stop where a dispatching
// measure ends its run (Train::runEndStop).
int servedStopCount(const Train& train) {
	return train.runEndStop >= 0 ? min(stopCount(train), train.runEndStop + 1) : stopCount(train);
}

// The stops where the train can still be boarded, from the first of its timetable: all of them, or those before the stop where a
// dispatching measure ends its run.
int boardableStopCount(const Train& train) {
	return train.runEndStop >= 0 ? min(stopCount(train), train.runEndStop) : stopCount(train);
}

// The last stop where the train stood (Node::StepStopped), or -1.
int lastStoodAt(const Train& train) {
	int last = -1;
	for (int stop = 0; stop < stopCount(train); ++stop)
		if (train.Stations[static_cast<size_t>(stop)].StepStopped > 0)
			last = stop;
	return last;
}

bool isStationName(const string& name) {
	return !name.empty() && !isNoneId(name);
}

int roundedSeconds(double seconds) {
	return static_cast<int>(lround(seconds));
}

// The journey the passenger is on, or null.
Journey* activeJourney(Passenger& passenger) {
	for (Journey& journey : passenger.Journeys)
		if (journey.ID == passenger.current_JourneyID && !journey.isFinished())
			return &journey;
	return nullptr;
}

// The number of trips before the trip with this ID; all of them when it is not there.
size_t tripsBefore(const Journey& journey, const string& tripId) {
	size_t before = 0;
	for (const Trip& trip : journey.Trips) {
		if (trip.TripID == tripId)
			break;
		++before;
	}
	return before;
}

// The passenger leaves the network at the station.
void leaveNetwork(Passenger& passenger, const string& station, const string& platformId, int now) {
	passenger.IsIntheNetwork = false;
	passenger.TimeExitedTheNetwork = now;
	passenger.StationExitedTheNetworkID = station;
	passenger.PlatformExitedTheNetworkID = platformId;
	passenger.current_location_ID = station;
	passenger.CurrentStatus = PassengerStatus::Outside;
	passenger.current_TripID = passenger.current_JourneyID = kNoneId;
	passenger.Current_Train_Boarded = passenger.Current_Train_To_Wait = kNoneId;
	passenger.Current_Arrival_Station = passenger.Current_Arrival_Platform = kNoneId;
	passenger.Current_WaitingStationID = passenger.Current_WaitingStationPlatformID = kNoneId;
}

// The passenger waits on the platform for the trip, which starts now.
void waitForTrip(Passenger& passenger, Trip& trip, int now) {
	trip.IsTripStarted = true;
	trip.Actual_Departure_Time = now;
	passenger.CurrentStatus = PassengerStatus::OnPlatform;
	passenger.IsIntheNetwork = true;
	passenger.current_location_ID = passenger.Current_WaitingStationID = trip.Dep_Station_ID;
	passenger.Current_WaitingStationPlatformID = trip.Dep_Station_Platform_ID;
	passenger.current_TripID = trip.TripID;
	passenger.Current_Train_To_Wait = trip.TrainServiceDescription;
	passenger.Current_Train_Boarded = kNoneId;
	passenger.Current_Arrival_Station = trip.Arr_Station_ID;
	passenger.Current_Arrival_Platform = trip.Arr_Station_Platform_ID;
}

// Where a passenger who gives up the trip `givenUp` (null for none) plans from at `now`: the time it is ready (seconds of the day)
// and the number of its platform. A passenger who has just alighted at alightedAtPlatform is there now. A passenger who waits for
// the trip is on its platform once the walk there is over; second 0 of the day is a time, and a trip that nothing has started
// has -9999, which leaves the passenger ready now without a platform.
pair<int, int> replanStart(const Trip* givenUp, int now, optional<int> alightedAtPlatform) {
	if (alightedAtPlatform || givenUp == nullptr || !givenUp->IsTripStarted || givenUp->Actual_Departure_Time < 0)
		return {now, alightedAtPlatform.value_or(0)};
	return {max(now, givenUp->Actual_Departure_Time), givenUp->depPlatformNumber};
}

// Whole minutes in seconds, rounded down and held to the range of an int, before the cast; not a number counts as the largest int.
int wholeMinutes(double seconds) {
	const double minutes = floor(seconds / 60.0);
	constexpr double kLargest = static_cast<double>(numeric_limits<int>::max());
	constexpr double kSmallest = static_cast<double>(numeric_limits<int>::min());
	if (std::isnan(minutes) || minutes >= kLargest)
		return numeric_limits<int>::max();
	return minutes <= kSmallest ? numeric_limits<int>::min() : static_cast<int>(minutes);
}

// Whether a time has both a planned and a predicted time.
bool hasLateness(double predicted, double planned) {
	return planned >= 0.0 && predicted >= 0.0;
}

// How much later than `reference` the lateness of a predicted time is, in whole minutes, with a millisecond of tolerance so that
// two times shifted by the same lateness count as equal; -1 where the time has no planned or no predicted time.
int extraLateMinutes(double predicted, double planned, double reference) {
	constexpr double kToleranceSeconds = 0.001;
	return hasLateness(predicted, planned) ? wholeMinutes(predicted - planned - reference + kToleranceSeconds) : -1;
}
// Updates flow and entrance-hold state after a waiting or held passenger was replanned.
void settleReplannedWaiting(SimulationRun& run, PassengerFlow& flow, int position, bool wasHeld,
	const string& oldStation, const string& oldPlatform, Journey* journey, int now) {
	Passenger& passenger = flow.passenger(position);
	if (!wasHeld) {
		flow.alighted(position);
		return;
	}
	if (passenger.IsIntheNetwork && passenger.CurrentStatus == PassengerStatus::OnPlatform
		&& passenger.Current_WaitingStationID == oldStation && passenger.Current_WaitingStationPlatformID == oldPlatform) {
		// Stay held at the same platform in the same FIFO place.
		passenger.CurrentStatus = PassengerStatus::HeldAtEntrance;
		flow.updateHeldTrain(position);
		return;
	}
	if (passenger.HeldSince >= 0) {
		if (journey != nullptr && now > passenger.HeldSince)
			journey->Heldtime += now - passenger.HeldSince;
		passenger.HeldSince = -1;
	}
	if (run.settings.passengerPlatformCapacityLimit)
		holdIfPlatformFull(run, flow, passenger, now);
	flow.alighted(position);
}
} // namespace

vector<int> predictionSignature(const passenger_routing::TrainTimetable& timetable, const TrainState& state) {
	vector<int> seen;
	if (!state.late && !state.slowed)
		return seen;
	const size_t count = timetable.stops.size();
	// The lateness of the first time ahead that has a planned and a predicted time.
	double reference = 0.0;
	bool found = false;
	for (size_t stop = state.firstStop; stop < count && !found; ++stop) {
		const double arrival = state.arrival(timetable, stop);
		const double departure = state.departure(timetable, stop);
		if (hasLateness(arrival, timetable.stops[stop].arrival)) {
			reference = arrival - timetable.stops[stop].arrival;
			found = true;
		} else if (hasLateness(departure, timetable.stops[stop].departure)) {
			reference = departure - timetable.stops[stop].departure;
			found = true;
		}
	}
	seen.reserve(3 + 2 * (count - min(count, state.firstStop)));
	seen.push_back(state.late ? 1 : 0);
	seen.push_back(state.slowed ? 1 : 0);
	seen.push_back(found ? wholeMinutes(reference) : -1);
	for (size_t stop = state.firstStop; stop < count; ++stop) {
		seen.push_back(extraLateMinutes(state.arrival(timetable, stop), timetable.stops[stop].arrival, reference));
		seen.push_back(extraLateMinutes(state.departure(timetable, stop), timetable.stops[stop].departure, reference));
	}
	return seen;
}

// The stops are followed by their index. A halt belongs to one call (Train::serviceStopOf), so a train that visits a station twice
// marks only the call it stands at as stood at.
int advanceFirstStop(const Train& train, int from) {
	const int count = stopCount(train);
	if (train.OutOfSimulation)
		return count;
	// The stops before the last one where the train stood are behind it, however many it passed without standing.
	const int lastStood = lastStoodAt(train);
	int stop = max(0, from);
	while (stop < count) {
		const bool stoppedHere = train.Stations[static_cast<size_t>(stop)].StepStopped > 0;
		const bool stoppedLater = lastStood > stop;
		// Left a stop that it stood at, or passed a stop where it never stood because a later one was reached.
		if ((stoppedHere && (!train.StoppedForServiceStop || stoppedLater)) || (!stoppedHere && stoppedLater))
			++stop;
		else
			break;
	}
	return stop;
}

bool haltsAtStop(const Train& train, int stop) {
	constexpr double kEntryToleranceKm = 0.001;
	return std::fabs(train.Stations[static_cast<size_t>(stop)].X - train.Start_Node_X) >= kEntryToleranceKm;
}

vector<double> limitedRunningSeconds(const SimulationRun& run, const Train& train, const vector<double>& positions, double limitMs) {
	using passenger_routing::limitedStretchSeconds;
	vector<double> seconds(positions.size(), 0.0);
	if (positions.empty())
		return seconds;
	double total = 0.0;
	// The stretch from positions.front() up to here is counted, and positions before next are recorded.
	double counted = positions.front();
	size_t next = 1;
	// Counts the stretch from counted up to `to` at speed and records the positions on it.
	const auto runTo = [&](double to, double speed) {
		while (next < positions.size() && positions[next] <= to) {
			total += limitedStretchSeconds(positions[next] - counted, speed, limitMs);
			counted = positions[next];
			seconds[next++] = total;
		}
		if (to > counted) {
			total += limitedStretchSeconds(to - counted, speed, limitMs);
			counted = to;
		}
	};
	if (train.indexOfRoute >= 0 && static_cast<size_t>(train.indexOfRoute) < run.infra.routes.size()) {
		for (const Section& section : run.infra.routes[static_cast<size_t>(train.indexOfRoute)].sequence_of_block_sections)
			for (const Arc& arc : section.arcs_in_signalling_block_section) {
				if (next >= positions.size())
					return seconds;
				if (arc.endNode.X * 1000 <= counted)
					continue;
				// A gap between two arcs has no speed data.
				runTo(min(arc.startNode.X * 1000, positions.back()), 0.0);
				double speed = train.trackSpeedLimit(arc);
				if (train.max_train_speed > 0.0)
					speed = min(speed, train.max_train_speed);
				runTo(min(arc.endNode.X * 1000, positions.back()), speed);
			}
	}
	runTo(positions.back(), 0.0);
	return seconds;
}

passenger_routing::TrainProgress liveTrainProgress(const SimulationRun& run, const Train& train, int firstStop, int elapsed) {
	passenger_routing::TrainProgress progress;
	const int count = stopCount(train);
	const double start = run.settings.startingSimulationTime;
	progress.firstStop = static_cast<size_t>(max(0, firstStop));
	progress.now = start + elapsed;
	// The latest stop that the train reached gives the delay: its departure delay once it left, else its arrival delay. A train that
	// arrived late can make up the time while it stands, so the departure is the newer of the two.
	bool arrived = false;
	for (int stop = min(firstStop, count - 1); stop >= 0 && !arrived; --stop) {
		const size_t at = static_cast<size_t>(stop);
		if (train.StationDepartures[at] >= 0 && train.ScheduledDepartures[at] >= 0) {
			progress.delay = max(0.0, train.StationDepartures[at] - train.ScheduledDepartures[at]);
			arrived = true;
		} else if (train.StationArrivals[at] >= 0 && train.ScheduledArrivals[at] >= 0) {
			progress.delay = max(0.0, train.StationArrivals[at] - train.ScheduledArrivals[at]);
			arrived = true;
		}
	}
	// Before its first arrival, a train that entered later than it could is as late as its entry.
	const bool entered = train.CanEnter && train.earliestActiveTrajectoryIndex >= 0;
	if (!arrived && entered)
		progress.delay = max(0.0, train.earliestActiveTrajectoryIndex * timestep - train.departure_time);
	progress.standing = firstStop >= 0 && firstStop < count && train.StoppedForServiceStop && train.Stations[static_cast<size_t>(firstStop)].StepStopped > 0;
	if (progress.standing) {
		const double planned = train.ScheduledDepartures[static_cast<size_t>(firstStop)];
		if (planned >= 0 && planned + progress.delay < elapsed)
			progress.delay = elapsed - planned;
	}

	// The route position of a stop in metres; only a stop on the route has one.
	const auto onRoute = [&train](int stop) { return train.stationBlockSection[stop] >= 0; };
	const auto position = [&train](int stop) { return train.Stations[static_cast<size_t>(stop)].X * 1000; };
	// The next stop follows the real progress of the train as well: the stop where it stands, else the stop after the last one it
	// stood at.
	const int lastStood = lastStoodAt(train);
	int next = max(0, firstStop);
	if (lastStood >= 0)
		next = max(next, train.StoppedForServiceStop ? lastStood : lastStood + 1);
	// Where the head of the train is when it does not stand at a stop, and the time at which it sets off from there.
	double head = 0.0;
	double setOff = -1.0;
	if (!progress.standing) {
		// The position of the step before is known in every phase of this step: passengers who enter plan before the trains move.
		const int last = elapsed - 1;
		const bool moving = entered && train.earliestActiveTrajectoryIndex <= last && last >= 0
			&& static_cast<size_t>(last) < train.instant_spatial_position.size()
			&& std::isfinite(train.instant_spatial_position[static_cast<size_t>(last)]);
		head = moving ? train.instant_spatial_position[static_cast<size_t>(last)] : train.Start_Node_X * 1000;
		while (next < count && !(onRoute(next) && position(next) >= head))
			++next;
		// A train that has not run a step yet sets off when it may enter.
		setOff = moving ? start + last * timestep : start + max(static_cast<double>(elapsed), train.departure_time);
		// A train that has not reached its next stop is at least as late as the time since it was due there: at its planned
		// arrival, or at the planned departure of a stop without one. A train that stands at a stop, also while it is held at the
		// start of its dwell with its head short of the platform, is not on its way there.
		for (int stop = next; stop < count && !train.StoppedForServiceStop; ++stop) {
			const size_t at = static_cast<size_t>(stop);
			const double due = train.ScheduledArrivals[at] >= 0 ? train.ScheduledArrivals[at] : train.ScheduledDepartures[at];
			if (onRoute(stop) && due >= 0) {
				progress.delay = max(progress.delay, elapsed - due);
				break;
			}
		}
	}
	progress.nextStop = static_cast<size_t>(next);

	// A breakdown with a speed cap; Active_Train_Breakdown gives a hold first, and otherwise the lowest cap.
	const auto isCap = [](const SimulationIncident* incident) {
		return incident != nullptr && incident->hasReducedSpeed && incident->reducedSpeedKmh > 0.0 && std::isfinite(incident->reducedSpeedKmh);
	};
	const SimulationIncident* cap = train.OutOfSimulation ? nullptr : Active_Train_Breakdown(run, train.trainDescription, elapsed);
	if (!isCap(cap))
		return progress;
	progress.limited = true;
	// Where the train sets off from when it does not stand at a stop, then the stops ahead on its route as far as they follow each
	// other along it.
	vector<double> positions;
	if (!progress.standing)
		positions.push_back(head);
	// seconds[offset + i] below is the time to stop ahead[i]. A stop off the route is passed over.
	const size_t offset = positions.size();
	vector<int> ahead;
	for (int stop = next; stop < count; ++stop) {
		if (!onRoute(stop))
			continue;
		if (!positions.empty() && position(stop) < positions.back())
			break;
		positions.push_back(position(stop));
		ahead.push_back(stop);
	}
	// The caps that apply now, as (end, limit in m/s), by end. Between two ends the lowest cap of those that still apply limits the
	// train, as the simulator takes the lowest cap at every step. A cap that starts later is not foreseen.
	vector<pair<double, double>> caps;
	for (const SimulationIncident* incident : Active_Train_Breakdowns(run, train.trainDescription, elapsed))
		if (isCap(incident))
			caps.emplace_back(incident->hasEndSeconds || incident->endSeconds != 0.0 ? start + incident->endSeconds : passenger_routing::kUnreached,
				incident->reducedSpeedKmh / 3.6);
	sort(caps.begin(), caps.end());
	for (size_t c = caps.size(); c-- > 1;)
		caps[c - 1].second = min(caps[c - 1].second, caps[c].second);
	for (size_t c = 0; c < caps.size(); ++c) {
		// A cap that ends with the one before adds no period.
		if (c > 0 && caps[c].first == caps[c - 1].first)
			continue;
		passenger_routing::LimitPeriod period;
		period.ends = caps[c].first;
		const vector<double> seconds = limitedRunningSeconds(run, train, positions, caps[c].second);
		// A run is known between two stops on the route that follow each other.
		period.runs.assign(static_cast<size_t>(max(0, count)), -1.0);
		for (size_t i = 1; i < ahead.size(); ++i)
			if (ahead[i] == ahead[i - 1] + 1)
				period.runs[static_cast<size_t>(ahead[i - 1])] = seconds[offset + i] - seconds[offset + i - 1];
		// The run to the stop ahead counts under the cap that applies when the train sets off.
		if (offset > 0 && !ahead.empty() && progress.nextArrival < 0.0 && setOff < period.ends)
			progress.nextArrival = setOff + seconds[offset];
		progress.limits.push_back(std::move(period));
	}
	return progress;
}

TrainState liveTrainState(const SimulationRun& run, const Train& train, const passenger_routing::TrainTimetable& timetable, int firstStop, int elapsed) {
	TrainState state = passenger_routing::predictTrainState(timetable, liveTrainProgress(run, train, firstStop, elapsed),
		run.settings.passengerLateThresholdSeconds);
	if (train.runEndStop >= 0)
		state.servedStops = static_cast<size_t>(servedStopCount(train));
	return state;
}

void PassengerJourneys::clear() {
	*this = PassengerJourneys();
}

void PassengerJourneys::beginPhase() {
	statesFresh_ = false;
	searches_.clear();
}

void PassengerJourneys::ensureBuilt(const SimulationRun& run) {
	if (router_)
		return;
	vector<passenger_routing::TrainTimetable> timetables;
	const double start = run.settings.startingSimulationTime;
	for (int n = 0; n < run.numTrains; ++n) {
		const Train& train = run.trains[static_cast<size_t>(n)];
		trainIndices_.emplace(train.trainDescription, n);
		passenger_routing::TrainTimetable timetable;
		for (int s = 0; s < stopCount(train); ++s) {
			const size_t at = static_cast<size_t>(s);
			const string& name = train.Stations[at].stationName;
			int station = -1;
			if (isStationName(name))
				station = stationIndices_.try_emplace(name, static_cast<int>(stationIndices_.size())).first->second;
			timetable.stops.push_back({station, train.stationPlatformNumber[at],
				train.ScheduledArrivals[at] >= 0 ? train.ScheduledArrivals[at] + start : -1.0,
				// A train cannot be boarded at a stop where it does not halt.
				train.ScheduledDepartures[at] >= 0 && haltsAtStop(train, s) ? train.ScheduledDepartures[at] + start : -1.0});
		}
		timetables.push_back(std::move(timetable));
	}
	router_ = make_unique<passenger_routing::Router>(std::move(timetables), stationIndices_.size(), run.settings.passengerWalkingSecondsPerPlatform);
	watches_.assign(static_cast<size_t>(run.numTrains), Watch());
}

void PassengerJourneys::ensureStates(const SimulationRun& run, int t) {
	if (statesFresh_ && statesTime_ == t)
		return;
	states_.resize(static_cast<size_t>(run.numTrains));
	for (int n = 0; n < run.numTrains; ++n) {
		Watch& watch = watches_[static_cast<size_t>(n)];
		const Train& train = run.trains[static_cast<size_t>(n)];
		watch.firstStop = advanceFirstStop(train, watch.firstStop);
		states_[static_cast<size_t>(n)] = liveTrainState(run, train, router_->trains()[static_cast<size_t>(n)], watch.firstStop, t);
	}
	statesFresh_ = true;
	statesTime_ = t;
	searches_.clear();
}

int PassengerJourneys::stationIndex(const string& name) const {
	const auto found = stationIndices_.find(name);
	return found == stationIndices_.end() ? -1 : found->second;
}

int PassengerJourneys::trainIndex(const string& description) const {
	const auto found = trainIndices_.find(description);
	return found == trainIndices_.end() ? -1 : found->second;
}

bool PassengerJourneys::callsAt(const SimulationRun& run, int train, const string& station, const string& platformId) const {
	const Train& T = run.trains[static_cast<size_t>(train)];
	for (int s = static_cast<int>(states_[static_cast<size_t>(train)].firstStop); s < boardableStopCount(T); ++s)
		if (haltsAtStop(T, s) && T.Stations[static_cast<size_t>(s)].stationName == station
			&& T.Stations[static_cast<size_t>(s)].stationPlatformId == platformId)
			return true;
	return false;
}

Plan PassengerJourneys::planFrom(const SimulationRun& run, const Journey& journey, const string& origin, int t, int readyAt, int platformNumber,
	size_t ridden) {
	const int from = stationIndex(origin);
	const int to = stationIndex(journey.Arr_Station_ID);
	if (from < 0 || to < 0)
		return Plan();
	ensureStates(run, t);
	// The journey may use passengerMaxTransfers + 1 trains, the trains it has ridden included. A passenger who has used them up,
	// such as one set down by a train that ended early, may still take one train.
	const int maxTransfers = max(0, run.settings.passengerMaxTransfers);
	const int transfers = ridden >= static_cast<size_t>(maxTransfers) ? 0 : maxTransfers - static_cast<int>(ridden);
	// A train that refused the passenger here is left out while it stands at this station.
	vector<TrainState> without;
	for (const auto& [description, station] : journey.RefusedBy) {
		const int train = trainIndex(description);
		if (station != origin || train < 0)
			continue;
		const Train& T = run.trains[static_cast<size_t>(train)];
		const size_t first = states_[static_cast<size_t>(train)].firstStop;
		if (first >= static_cast<size_t>(stopCount(T)) || T.Stations[first].stationName != origin)
			continue;
		if (without.empty())
			without = states_;
		without[static_cast<size_t>(train)].firstStop = first + 1;
	}
	if (!without.empty())
		return router_->search(static_cast<size_t>(from), readyAt, transfers, without, platformNumber).plan(static_cast<size_t>(to));
	const auto key = make_tuple(from, readyAt, platformNumber, transfers);
	auto found = searches_.find(key);
	if (found == searches_.end())
		found = searches_.emplace(key, router_->search(static_cast<size_t>(from), readyAt, transfers, states_, platformNumber)).first;
	return found->second.plan(static_cast<size_t>(to));
}

void PassengerJourneys::planInto(SimulationRun& run, Passenger& passenger, Journey& journey, size_t keptTrips, int t, int readyAt, int platformNumber, bool isReplan,
	const Plan* given) {
	const int now = t + run.settings.startingSimulationTime;
	const string station = passenger.Current_WaitingStationID;
	const string platformId = passenger.Current_WaitingStationPlatformID;
	// The first plan of a journey with given legs is the last of them.
	if (journey.FirstPlanArrival == -9999 && !journey.Trips.empty())
		journey.FirstPlanArrival = journey.Trips.back().Planned_Arrival_Time;
	journey.Trips.erase(next(journey.Trips.begin(), static_cast<ptrdiff_t>(min(keptTrips, journey.Trips.size()))), journey.Trips.end());
	if (isReplan)
		journey.IsReplanned = true;
	if (station == journey.Arr_Station_ID) {
		// The passenger is at the destination already.
		journey.Actual_Arrival_Time = now;
		journey.totalJourneyArrivalDelay = journey.arrivalDelay(now, journey.Trips.empty() ? 0 : journey.Trips.back().totalArrivalDelay);
		journey.IsJourneyCompleted = true;
		journey.Outcome = JourneyOutcome::Completed;
		journey.N_Trips = static_cast<int>(journey.Trips.size());
		leaveNetwork(passenger, station, platformId, now);
		return;
	}
	const Plan plan = given != nullptr ? *given : planFrom(run, journey, station, t, readyAt, platformNumber, keptTrips);
	journey.N_Trips = static_cast<int>(journey.Trips.size());
	if (!plan.reached() || plan.legs.empty()) {
		journey.Outcome = JourneyOutcome::NoRoute;
		EG_LOG(SimPax, Info) << "Passenger " << passenger.ID << " found no route from " << station << " to " << journey.Arr_Station_ID
							 << " for journey " << journey.ID << "\n";
		leaveNetwork(passenger, station, platformId, now);
		return;
	}
	for (const Leg& leg : plan.legs) {
		const Train& T = run.trains[leg.train];
		const Node& board = T.Stations[leg.boardStop];
		const Node& alight = T.Stations[leg.alightStop];
		const passenger_routing::Stop& boardPlan = router_->trains()[leg.train].stops[leg.boardStop];
		const passenger_routing::Stop& alightPlan = router_->trains()[leg.train].stops[leg.alightStop];
		Trip trip;
		trip.TripID = journey.ID + ".live" + to_string(++journey.LiveTripCount);
		trip.JourneyID = journey.ID;
		trip.Dep_Station_ID = board.stationName;
		trip.Arr_Station_ID = alight.stationName;
		trip.Dep_Station_Platform_ID = board.stationPlatformId;
		trip.Arr_Station_Platform_ID = alight.stationPlatformId;
		trip.depPlatformNumber = T.stationPlatformNumber[leg.boardStop];
		trip.TrainServiceDescription = T.trainDescription;
		if (boardPlan.departure >= 0)
			trip.Planned_Departure_Time = roundedSeconds(boardPlan.departure);
		if (alightPlan.arrival >= 0)
			trip.Planned_Arrival_Time = roundedSeconds(alightPlan.arrival);
		journey.Trips.push_back(std::move(trip));
	}
	journey.N_Trips = static_cast<int>(journey.Trips.size());
	if (journey.FirstPlanArrival == -9999)
		journey.FirstPlanArrival = journey.Trips.back().Planned_Arrival_Time;
	Trip& first = *next(journey.Trips.begin(), static_cast<ptrdiff_t>(keptTrips));
	// A passenger who stands on another platform walks to the platform of the first train, as at a transfer.
	const int walking = static_cast<int>(passenger_routing::walkingSeconds(platformNumber, first.depPlatformNumber, run.settings.passengerWalkingSecondsPerPlatform));
	journey.Walkingtime += walking;
	waitForTrip(passenger, first, readyAt + walking);
}

void PassengerJourneys::replanWaiting(SimulationRun& run, Passenger& passenger, int t, optional<int> alightedAtPlatform, const Plan* given) {
	Journey* journey = activeJourney(passenger);
	if (journey == nullptr)
		return;
	const int now = t + run.settings.startingSimulationTime;
	const size_t kept = tripsBefore(*journey, passenger.current_TripID);
	const Trip* givenUp = kept < journey->Trips.size() ? &*next(journey->Trips.begin(), static_cast<ptrdiff_t>(kept)) : nullptr;
	// The line the passenger stands in: the train, station and platform of the trip that is given up, and since when.
	optional<Trip> line;
	// Second 0 of the day is a time; a trip that nothing has started has -9999.
	if (givenUp != nullptr && givenUp->IsTripStarted && givenUp->Actual_Departure_Time >= 0) {
		if (alightedAtPlatform) {
			// The passenger has just alighted and has not set off for the platform of the train that is given up, so the
			// walk there that the transfer counted is not made: the plan starts now, on the platform of the arrival.
			journey->Walkingtime -= max(0, givenUp->Actual_Departure_Time - now);
		} else {
			line = *givenUp;
		}
	}
	// A passenger still walking to the platform cannot leave before getting there.
	const auto [readyAt, platformNumber] = replanStart(givenUp, now, alightedAtPlatform);
	planInto(run, passenger, *journey, kept, t, readyAt, platformNumber, true, given);
	if (!line)
		return;
	// A passenger whose new plan boards the same train at the same platform keeps the place in its line and goes on waiting from
	// the same time. One who leaves the line counts the time waited in it and goes to the back of the next one.
	Trip* first = nullptr;
	if (passenger.CurrentStatus == PassengerStatus::OnPlatform && activeJourney(passenger) == journey) {
		const size_t waitedFor = tripsBefore(*journey, passenger.current_TripID);
		if (waitedFor < journey->Trips.size())
			first = &*next(journey->Trips.begin(), static_cast<ptrdiff_t>(waitedFor));
	}
	if (first != nullptr && first->TrainServiceDescription == line->TrainServiceDescription && first->Dep_Station_ID == line->Dep_Station_ID
		&& first->Dep_Station_Platform_ID == line->Dep_Station_Platform_ID)
		first->Actual_Departure_Time = line->Actual_Departure_Time;
	else if (now > line->Actual_Departure_Time)
		journey->Waitingtime += now - line->Actual_Departure_Time;
}

void PassengerJourneys::missedTrain(SimulationRun& run, Passenger& passenger, int t, optional<int> alightedAtPlatform) {
	// The trips before the one that the passenger waits for were ridden, so a missed train after them was a connection.
	Journey* journey = activeJourney(passenger);
	if (journey != nullptr && tripsBefore(*journey, passenger.current_TripID) > 0 && journey->MissedConnections < numeric_limits<uint16_t>::max())
		++journey->MissedConnections;
	replanWaiting(run, passenger, t, alightedAtPlatform);
}

optional<Plan> PassengerJourneys::standingDirectPlan(const SimulationRun& run, const Passenger& passenger, const Journey& journey,
	const Trip& current, int t, optional<int> alightedAtPlatform, int onlyTrain) const {
	const string& station = passenger.Current_WaitingStationID;
	const string& platformId = passenger.Current_WaitingStationPlatformID;
	const int currentTrain = trainIndex(current.TrainServiceDescription);
	// The train that the passenger has just left is not another train.
	const auto self = find_if(journey.Trips.begin(), journey.Trips.end(), [&current](const Trip& trip) { return trip.TripID == current.TripID; });
	const int leftTrain = self != journey.Trips.begin() ? trainIndex(prev(self)->TrainServiceDescription) : -1;
	const int lastTrain = trainIndex(journey.Trips.back().TrainServiceDescription);
	if (lastTrain < 0)
		return nullopt;
	const double start = run.settings.startingSimulationTime;
	// When the plan reaches the destination: the arrival of its last train there, as the routing sees that train now.
	const Train& last = run.trains[static_cast<size_t>(lastTrain)];
	const passenger_routing::TrainTimetable& lastTimetable = router_->trains()[static_cast<size_t>(lastTrain)];
	const TrainState& lastState = states_[static_cast<size_t>(lastTrain)];
	double planArrival = passenger_routing::kUnreached;
	for (size_t s = lastState.firstStop; s < static_cast<size_t>(servedStopCount(last)); ++s)
		if (last.Stations[s].stationName == journey.Arr_Station_ID && lastTimetable.stops[s].arrival >= 0) {
			planArrival = lastState.arrival(lastTimetable, s);
			break;
		}
	if (planArrival == passenger_routing::kUnreached)
		return nullopt;
	optional<Plan> best;
	for (int n = 0; n < run.numTrains; ++n) {
		const Train& T = run.trains[static_cast<size_t>(n)];
		// Only a train that stands at the platform now.
		if ((onlyTrain >= 0 && n != onlyTrain) || n == currentTrain || n == leftTrain || T.Current_OnBoard_Passengers >= T.MAX_OnBoard_Passengers
			|| !T.StoppedForServiceStop || T.OutOfSimulation || T.CurrentServiceStop != station
			|| T.CurrentServiceStopPlatform != platformId)
			continue;
		const TrainState& state = states_[static_cast<size_t>(n)];
		const passenger_routing::TrainTimetable& timetable = router_->trains()[static_cast<size_t>(n)];
		const size_t board = state.firstStop;
		// The timetable of the router has no departure where the train does not halt.
		if (board >= static_cast<size_t>(boardableStopCount(T)) || T.Stations[board].stationName != station
			|| T.Stations[board].stationPlatformId != platformId || timetable.stops[board].departure < 0)
			continue;
		const double departure = state.departure(timetable, board);
		// A passenger who alighted walks from the arrival platform; one who waits is ready when the walk to the platform ends.
		const double now = t + start;
		const double ready = alightedAtPlatform
			? now + passenger_routing::walkingSeconds(*alightedAtPlatform, T.stationPlatformNumber[board], run.settings.passengerWalkingSecondsPerPlatform)
			: (current.IsTripStarted && current.Actual_Departure_Time >= 0 ? max(now, static_cast<double>(current.Actual_Departure_Time)) : now);
		if (ready > departure)
			continue;
		for (size_t s = board + 1; s < static_cast<size_t>(servedStopCount(T)); ++s) {
			if (T.Stations[s].stationName != journey.Arr_Station_ID || timetable.stops[s].arrival < 0)
				continue;
			const double arrival = state.arrival(timetable, s);
			if (arrival <= planArrival && (!best || arrival < best->arrival))
				best = Plan{{Leg{static_cast<size_t>(n), board, s, departure, arrival}}, arrival};
			break;
		}
	}
	return best;
}

void PassengerJourneys::startJourney(SimulationRun& run, Passenger& passenger, int t) {
	ensureBuilt(run);
	Journey* journey = activeJourney(passenger);
	if (journey == nullptr)
		return;
	if (journey->Mode == JourneyMode::Live) {
		passenger.Current_WaitingStationID = journey->Dep_Station_ID;
		passenger.Current_WaitingStationPlatformID = kNoneId;
		planInto(run, passenger, *journey, 0, t, t + run.settings.startingSimulationTime, 0, false);
	} else {
		checkWaiting(run, passenger, t);
	}
}

void PassengerJourneys::checkWaiting(SimulationRun& run, Passenger& passenger, int t, optional<int> alightedAtPlatform) {
	ensureBuilt(run);
	if (!passenger.IsIntheNetwork || passenger.CurrentStatus != PassengerStatus::OnPlatform)
		return;
	const Journey* journey = activeJourney(passenger);
	if (journey == nullptr)
		return;
	const auto trip = find_if(journey->Trips.begin(), journey->Trips.end(),
		[&passenger](const Trip& candidate) { return candidate.TripID == passenger.current_TripID; });
	if (trip == journey->Trips.end())
		return;
	const int train = trainIndex(trip->TrainServiceDescription);
	if (train < 0)
		return;
	ensureStates(run, t);
	if (!callsAt(run, train, passenger.Current_WaitingStationID, passenger.Current_WaitingStationPlatformID)) {
		missedTrain(run, passenger, t, alightedAtPlatform);
	} else if (!takesToTripEnd(run, train, passenger, *trip)) {
		// A measure ended the run of the train before the stop where the passenger would leave it: not a missed connection.
		replanWaiting(run, passenger, t, alightedAtPlatform);
	} else if (const optional<Plan> direct = standingDirectPlan(run, passenger, *journey, *trip, t, alightedAtPlatform)) {
		// A train that stands here and goes direct to the destination in time is taken instead of the plan.
		replanWaiting(run, passenger, t, alightedAtPlatform, &*direct);
	} else if (states_[static_cast<size_t>(train)].late || states_[static_cast<size_t>(train)].slowed) {
		// The prediction of the train is new to this passenger.
		switchToEarlierPlan(run, passenger, t, alightedAtPlatform);
	}
}

void PassengerJourneys::offerStandingTrain(SimulationRun& run, PassengerFlow& flow, int train, int t) {
	ensureBuilt(run);
	const Train& T = run.trains[static_cast<size_t>(train)];
	ensureStates(run, t);
	const int stop = T.serviceStopOf(T.CurrentServiceStop);
	if (stop < 0 || static_cast<size_t>(stop) != states_[static_cast<size_t>(train)].firstStop)
		return;
	const Node& node = T.Stations[static_cast<size_t>(stop)];
	for (const int position : flow.waitingAt(flow.platformNumber(node.stationName, node.stationPlatformId))) {
		Passenger& passenger = flow.passenger(position);
		const Journey* journey = activeJourney(passenger);
		if (journey == nullptr || passenger.Current_Train_To_Wait == T.trainDescription)
			continue;
		const auto trip = find_if(journey->Trips.begin(), journey->Trips.end(),
			[&passenger](const Trip& candidate) { return candidate.TripID == passenger.current_TripID; });
		if (trip == journey->Trips.end())
			continue;
		if (const optional<Plan> direct = standingDirectPlan(run, passenger, *journey, *trip, t, nullopt, train)) {
			replanWaiting(run, passenger, t, nullopt, &*direct);
			flow.alighted(position);
		}
	}
}

void PassengerJourneys::replanRefused(SimulationRun& run, Passenger& passenger, const Train& full, int t) {
	ensureBuilt(run);
	if (Journey* journey = activeJourney(passenger))
		journey->RefusedBy.emplace_back(full.trainDescription, passenger.Current_WaitingStationID);
	replanWaiting(run, passenger, t);
}

void PassengerJourneys::flushLeftStops(SimulationRun& run, PassengerFlow& flow, int train, int t) {
	Watch& watch = watches_[static_cast<size_t>(train)];
	const Train& T = run.trains[static_cast<size_t>(train)];
	const int trainId = flow.trainNumber(T.trainDescription);
	const int now = t + run.settings.startingSimulationTime;
	for (; watch.handled < watch.firstStop; ++watch.handled) {
		const Node& stop = T.Stations[static_cast<size_t>(watch.handled)];
		for (const int position : flow.waitingFor(flow.platformNumber(stop.stationName, stop.stationPlatformId), trainId)) {
			Passenger& passenger = flow.passenger(position);
			const bool wasHeld = passenger.CurrentStatus == PassengerStatus::HeldAtEntrance;
			const string oldStation = passenger.Current_WaitingStationID;
			const string oldPlatform = passenger.Current_WaitingStationPlatformID;
			Journey* const journey = activeJourney(passenger);
			missedTrain(run, passenger, t);
			settleReplannedWaiting(run, flow, position, wasHeld, oldStation, oldPlatform, journey, now);
		}
	}
}

bool PassengerJourneys::takesToTripEnd(const SimulationRun& run, int train, const Passenger& passenger, const Trip& trip) const {
	const Train& T = run.trains[static_cast<size_t>(train)];
	if (T.runEndStop < 0)
		return true;
	const int served = servedStopCount(T);
	for (int board = static_cast<int>(states_[static_cast<size_t>(train)].firstStop); board < boardableStopCount(T); ++board) {
		const Node& node = T.Stations[static_cast<size_t>(board)];
		if (!haltsAtStop(T, board) || node.stationName != passenger.Current_WaitingStationID
			|| node.stationPlatformId != passenger.Current_WaitingStationPlatformID)
			continue;
		for (int alight = board + 1; alight < served; ++alight)
			if (T.Stations[static_cast<size_t>(alight)].stationName == trip.Arr_Station_ID)
				return true;
	}
	return false;
}

void PassengerJourneys::replanCutTrips(SimulationRun& run, PassengerFlow& flow, int train, int t) {
	const Train& T = run.trains[static_cast<size_t>(train)];
	const int trainId = flow.trainNumber(T.trainDescription);
	if (trainId < 0)
		return;
	vector<int> waiting;
	for (int stop = watches_[static_cast<size_t>(train)].firstStop; stop < stopCount(T); ++stop) {
		const Node& node = T.Stations[static_cast<size_t>(stop)];
		const int platform = flow.platformNumber(node.stationName, node.stationPlatformId);
		if (platform < 0)
			continue;
		const vector<int> found = flow.waitingFor(platform, trainId);
		waiting.insert(waiting.end(), found.begin(), found.end());
	}
	// In the order of the passenger list, and once for a passenger at a platform where the train calls twice.
	sort(waiting.begin(), waiting.end());
	waiting.erase(unique(waiting.begin(), waiting.end()), waiting.end());
	ensureStates(run, t);
	for (const int position : waiting) {
		Passenger& passenger = flow.passenger(position);
		const Journey* journey = activeJourney(passenger);
		if (journey == nullptr)
			continue;
		const auto trip = find_if(journey->Trips.begin(), journey->Trips.end(),
			[&passenger](const Trip& candidate) { return candidate.TripID == passenger.current_TripID; });
		if (trip == journey->Trips.end())
			continue;
		// As in checkWaiting: a train that no longer calls here is a missed train, one that ends before the stop where the
		// passenger would leave it is not.
		if (!callsAt(run, train, passenger.Current_WaitingStationID, passenger.Current_WaitingStationPlatformID)) {
			missedTrain(run, passenger, t);
			flow.alighted(position);
		} else if (!takesToTripEnd(run, train, passenger, *trip)) {
			replanWaiting(run, passenger, t);
			flow.alighted(position);
		}
	}
}

void PassengerJourneys::setDownEarly(SimulationRun& run, Passenger& passenger, const Train& train, int stop, int t, const Plan* given) {
	Journey* journey = activeJourney(passenger);
	if (journey == nullptr)
		return;
	const auto trip = find_if(journey->Trips.begin(), journey->Trips.end(),
		[&passenger](const Trip& candidate) { return candidate.TripID == passenger.current_TripID; });
	if (trip == journey->Trips.end())
		return;
	const int now = t + run.settings.startingSimulationTime;
	const Node& node = train.Stations[static_cast<size_t>(stop)];
	// The trip is cut short below, so keep the planned arrival of the first plan while it is still there.
	if (journey->FirstPlanArrival == -9999)
		journey->FirstPlanArrival = journey->Trips.back().Planned_Arrival_Time;
	const size_t tripIndex = static_cast<size_t>(distance(journey->Trips.begin(), trip));
	passenger.CurrentStatus = PassengerStatus::OnPlatform;
	passenger.current_location_ID = passenger.Current_WaitingStationID = node.stationName;
	passenger.Current_WaitingStationPlatformID = node.stationPlatformId;
	passenger.Current_Train_Boarded = passenger.Current_Train_To_Wait = kNoneId;
	// A passenger who boarded while the train stood at the stop where it now ends has ridden nothing. The trip is dropped and
	// planned again from the stop, as for a passenger who never boarded, so it counts neither as a trip nor as a train ridden.
	const bool rodeNothing = trip->Dep_Station_ID == node.stationName && trip->Actual_Boarding_Time != -9999
		&& trip->Actual_Boarding_Time - run.settings.startingSimulationTime >= t - node.StepStopped - 1;
	if (rodeNothing) {
		planInto(run, passenger, *journey, tripIndex, t, now, train.stationPlatformNumber[static_cast<size_t>(stop)], true, given);
		return;
	}
	trip->Arr_Station_ID = node.stationName;
	trip->Arr_Station_Platform_ID = node.stationPlatformId;
	trip->Actual_Arrival_Time = now;
	if (train.ScheduledArrivals[stop] >= 0)
		trip->Planned_Arrival_Time = roundedSeconds(train.ScheduledArrivals[stop] + run.settings.startingSimulationTime);
	if (trip->Planned_Arrival_Time != -9999)
		trip->totalArrivalDelay = trip->Actual_Arrival_Time - trip->Planned_Arrival_Time;
	trip->IsTripCompleted = true;
	planInto(run, passenger, *journey, tripIndex + 1, t, now, train.stationPlatformNumber[static_cast<size_t>(stop)], true, given);
}

void PassengerJourneys::setDownEndedTrain(SimulationRun& run, PassengerFlow& flow, int train, int t) {
	Train& T = run.trains[static_cast<size_t>(train)];
	const vector<int> onBoard = flow.takeAllOnBoard(T.trainDescription);
	if (onBoard.empty())
		return;
	// The train ended at the last stop where it stood.
	int stop = stopCount(T) - 1;
	while (stop > 0 && T.Stations[static_cast<size_t>(stop)].StepStopped == 0)
		--stop;
	if (stop < 0)
		return;
	// They alight at that stop, so the counts of its call have them.
	StopCallCounts& call = run.stopCalls.at(train, stop);
	for (const int position : onBoard) {
		--T.Current_OnBoard_Passengers;
		++call.alighted;
		setDownEarly(run, flow.passenger(position), T, stop, t);
		flow.alighted(position);
	}
	call.loadAfter = T.Current_OnBoard_Passengers;
}

double PassengerJourneys::tripsArrival(const SimulationRun& run, const Journey& journey, size_t from, double readyAt, int platformNumber,
	const string& platformId, int onBoardAt) const {
	double at = readyAt;
	int platform = platformNumber;
	const auto first = next(journey.Trips.begin(), static_cast<ptrdiff_t>(min(from, journey.Trips.size())));
	for (auto trip = first; trip != journey.Trips.end(); ++trip) {
		const string& boardPlatform = trip == first ? platformId : trip->Dep_Station_Platform_ID;
		const int train = trainIndex(trip->TrainServiceDescription);
		if (train < 0)
			return passenger_routing::kUnreached;
		const Train& T = run.trains[static_cast<size_t>(train)];
		const passenger_routing::TrainTimetable& timetable = router_->trains()[static_cast<size_t>(train)];
		const TrainState& state = states_[static_cast<size_t>(train)];
		const size_t count = timetable.stops.size();
		const size_t boardable = min(count, static_cast<size_t>(boardableStopCount(T)));
		const size_t served = min(count, static_cast<size_t>(servedStopCount(T)));
		// The first call that the train still makes at the station and platform of the trip and that the passenger reaches in time,
		// as the router judges it (TrainState::boardingDeadline). A call without times is reached.
		// A passenger on board stays on the train from the stop where it stands.
		const bool onBoard = trip == first && onBoardAt >= 0;
		size_t board = onBoard ? static_cast<size_t>(onBoardAt) : state.firstStop;
		for (; !onBoard && board < boardable; ++board) {
			const Node& node = T.Stations[board];
			if (node.stationName != trip->Dep_Station_ID || (!isNoneId(boardPlatform) && node.stationPlatformId != boardPlatform)
				|| !haltsAtStop(T, static_cast<int>(board)))
				continue;
			const double deadline = state.boardingDeadline(timetable, board);
			const double walking = passenger_routing::walkingSeconds(platform, timetable.stops[board].platformNumber, run.settings.passengerWalkingSecondsPerPlatform);
			if (deadline < 0 || at + walking <= deadline)
				break;
		}
		if (board >= boardable)
			return passenger_routing::kUnreached;
		// The first later call at the arrival station where a search leaves the train (TrainState::alightingTime). A call there
		// without an arrival leaves the time unknown.
		size_t alight = board + 1;
		bool callWithoutArrival = false;
		for (; alight < served; ++alight) {
			if (T.Stations[alight].stationName != trip->Arr_Station_ID)
				continue;
			if (state.alightingTime(timetable, alight) >= 0)
				break;
			callWithoutArrival = true;
		}
		if (alight >= served)
			return callWithoutArrival ? -1.0 : passenger_routing::kUnreached;
		at = state.alightingTime(timetable, alight);
		platform = timetable.stops[alight].platformNumber;
	}
	return at;
}

bool PassengerJourneys::switchToEarlierPlan(SimulationRun& run, Passenger& passenger, int t, optional<int> alightedAtPlatform) {
	Journey* journey = activeJourney(passenger);
	if (journey == nullptr)
		return false;
	const size_t kept = tripsBefore(*journey, passenger.current_TripID);
	if (kept >= journey->Trips.size())
		return false;
	const Trip& current = *next(journey->Trips.begin(), static_cast<ptrdiff_t>(kept));
	const int now = t + run.settings.startingSimulationTime;
	// The plan the passenger has starts on the platform of its first train, once the walk there is over. A plan whose times are
	// not known is kept.
	const auto [waitFrom, waitPlatform] = replanStart(&current, now, nullopt);
	const double planned = tripsArrival(run, *journey, kept, waitFrom, waitPlatform, passenger.Current_WaitingStationPlatformID);
	if (planned < 0)
		return false;
	// A new plan starts where replanWaiting starts it.
	const auto [readyAt, platformNumber] = replanStart(&current, now, alightedAtPlatform);
	const optional<Plan> plan = earlierPlan(run, *journey, passenger.Current_WaitingStationID, t, readyAt, platformNumber, kept, planned);
	if (!plan)
		return false;
	replanWaiting(run, passenger, t, alightedAtPlatform, &*plan);
	return true;
}

optional<Plan> PassengerJourneys::earlierPlan(const SimulationRun& run, const Journey& journey, const string& origin, int t, int readyAt,
	int platformNumber, size_t ridden, double planned) {
	if (planned < 0)
		return nullopt;
	Plan plan = planFrom(run, journey, origin, t, readyAt, platformNumber, ridden);
	if (!plan.reached() || plan.legs.empty() || plan.arrival + kSwitchMarginSeconds >= planned)
		return nullopt;
	return plan;
}

bool PassengerJourneys::hasRoomToBoard(const SimulationRun& run, const PassengerFlow& flow, const Leg& leg) const {
	const Train& T = run.trains[leg.train];
	const Node& board = T.Stations[leg.boardStop];
	const int platform = flow.platformNumber(board.stationName, board.stationPlatformId);
	const int train = flow.trainNumber(T.trainDescription);
	const size_t waiting = platform < 0 || train < 0 ? 0 : flow.waitingFor(platform, train).size();
	return static_cast<size_t>(max(0, T.Current_OnBoard_Passengers)) + waiting < static_cast<size_t>(max(0, T.MAX_OnBoard_Passengers));
}

bool PassengerJourneys::switchOnBoard(SimulationRun& run, PassengerFlow& flow, Passenger& passenger, int train, int stop, int t) {
	Journey* journey = activeJourney(passenger);
	if (journey == nullptr || stop < 0 || stop >= Train::kMaxTimetableStations)
		return false;
	const size_t kept = tripsBefore(*journey, passenger.current_TripID);
	if (kept >= journey->Trips.size())
		return false;
	Train& T = run.trains[static_cast<size_t>(train)];
	const Node& node = T.Stations[static_cast<size_t>(stop)];
	// A passenger whose trip ends here alights as usual, and one who boarded here was offered the earlier plans while waiting.
	const Trip& current = *next(journey->Trips.begin(), static_cast<ptrdiff_t>(kept));
	if (current.Arr_Station_ID == node.stationName || current.Dep_Station_ID == node.stationName)
		return false;
	const int now = t + run.settings.startingSimulationTime;
	const int platformNumber = T.currentServiceStopPlatformNumber;
	// Staying on board, and the rest of the plan, against a plan from the platform of the train now that counts the train as ridden.
	const double planned = tripsArrival(run, *journey, kept, now, platformNumber, node.stationPlatformId, stop);
	const optional<Plan> plan = earlierPlan(run, *journey, node.stationName, t, now, platformNumber, kept + 1, planned);
	// A plan that rides this train on from here changes further on: the passenger stays on board and looks again there. A passenger
	// does not leave the train for one that has no room.
	if (!plan || plan->legs.front().train == static_cast<size_t>(train) || !hasRoomToBoard(run, flow, plan->legs.front()))
		return false;
	--T.Current_OnBoard_Passengers;
	setDownEarly(run, passenger, T, stop, t, &*plan);
	return true;
}

vector<int> PassengerJourneys::trainsWithNewPrediction(const SimulationRun& run, int t) {
	ensureBuilt(run);
	ensureStates(run, t);
	predictions_.resize(states_.size());
	vector<int> changed;
	for (size_t n = 0; n < states_.size(); ++n) {
		vector<int> seen = predictionSignature(router_->trains()[n], states_[n]);
		if (seen == predictions_[n])
			continue;
		predictions_[n] = std::move(seen);
		if (!predictions_[n].empty())
			changed.push_back(static_cast<int>(n));
	}
	return changed;
}

int PassengerJourneys::offerEarlierPlansOnBoard(SimulationRun& run, PassengerFlow& flow, int train, int t) {
	ensureBuilt(run);
	const Train& T = run.trains[static_cast<size_t>(train)];
	if (!T.StoppedForServiceStop || T.OutOfSimulation)
		return 0;
	ensureStates(run, t);
	const TrainState& state = states_[static_cast<size_t>(train)];
	// Only at the stop where the train stands, and not at its last stop, where everyone alights: the last of its timetable, or the
	// stop where a dispatching measure ends its run (setDownEndedTrain sets everyone down there).
	const int stop = T.serviceStopOf(T.CurrentServiceStop);
	if (stop < 0 || static_cast<size_t>(stop) != state.firstStop || stop + 1 >= servedStopCount(T) || (!state.late && !state.slowed))
		return 0;
	// Once when the train comes to a stand, and again when its prediction changes while it stands.
	if (T.Stations[static_cast<size_t>(stop)].StepStopped != 1 && !binary_search(newPredictions_.begin(), newPredictions_.end(), train))
		return 0;
	int alighted = 0;
	for (const int position : flow.onBoard(T.trainDescription))
		if (switchOnBoard(run, flow, flow.passenger(position), train, stop, t)) {
			flow.alighted(position);
			++alighted;
		}
	return alighted;
}

void PassengerJourneys::offerEarlierPlans(SimulationRun& run, PassengerFlow& flow, const vector<int>& trains, int t) {
	vector<int> waiting;
	for (const int train : trains) {
		const Train& T = run.trains[static_cast<size_t>(train)];
		const int trainId = flow.trainNumber(T.trainDescription);
		if (trainId < 0)
			continue;
		for (int stop = static_cast<int>(states_[static_cast<size_t>(train)].firstStop); stop < boardableStopCount(T); ++stop) {
			const Node& node = T.Stations[static_cast<size_t>(stop)];
			const int platform = flow.platformNumber(node.stationName, node.stationPlatformId);
			if (platform < 0 || !haltsAtStop(T, stop))
				continue;
			const vector<int> found = flow.waitingFor(platform, trainId);
			waiting.insert(waiting.end(), found.begin(), found.end());
		}
	}
	// In the order of the passenger list, and once for a passenger at a platform where the train calls twice.
	sort(waiting.begin(), waiting.end());
	waiting.erase(unique(waiting.begin(), waiting.end()), waiting.end());
	const int now = t + run.settings.startingSimulationTime;
	for (const int position : waiting) {
		Passenger& passenger = flow.passenger(position);
		const bool wasHeld = passenger.CurrentStatus == PassengerStatus::HeldAtEntrance;
		const string oldStation = passenger.Current_WaitingStationID;
		const string oldPlatform = passenger.Current_WaitingStationPlatformID;
		Journey* const journey = activeJourney(passenger);
		if (switchToEarlierPlan(run, passenger, t, nullopt))
			settleReplannedWaiting(run, flow, position, wasHeld, oldStation, oldPlatform, journey, now);
	}
}

void PassengerJourneys::settleTrains(SimulationRun& run, PassengerFlow& flow, int t) {
	beginPhase();
	newPredictions_.clear();
	if (run.passengers.empty())
		return;
	ensureBuilt(run);
	// Every train is brought up to date before the first passenger plans, so that the plans see all of them as they are.
	for (int n = 0; n < run.numTrains; ++n) {
		Watch& watch = watches_[static_cast<size_t>(n)];
		watch.firstStop = advanceFirstStop(run.trains[static_cast<size_t>(n)], watch.firstStop);
	}
	for (int n = 0; n < run.numTrains; ++n) {
		flushLeftStops(run, flow, n, t);
		Watch& watch = watches_[static_cast<size_t>(n)];
		if (run.trains[static_cast<size_t>(n)].OutOfSimulation && !watch.ended) {
			watch.ended = true;
			setDownEndedTrain(run, flow, n, t);
		}
		if (run.trains[static_cast<size_t>(n)].runEndStop != watch.runEndStop) {
			watch.runEndStop = run.trains[static_cast<size_t>(n)].runEndStop;
			replanCutTrips(run, flow, n, t);
		}
	}
	newPredictions_ = trainsWithNewPrediction(run, t);
	offerEarlierPlans(run, flow, newPredictions_, t);
	admitHeldPassengers(run, flow, t + run.settings.startingSimulationTime);
}
