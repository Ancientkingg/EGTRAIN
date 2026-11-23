#include "simulation/PassengerJourneys.h"
#include "simulation/SimulationRun.h"
#include "util/Log.h"
#include <algorithm>
#include <cmath>
#include <iterator>

using namespace std;
using passenger_routing::Leg;
using passenger_routing::Plan;
using passenger_routing::TrainState;

namespace {
int stopCount(const Train& train) {
	return max(0, min(train.numStations, static_cast<int>(train.Stations.size())));
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
} // namespace

// The stops are followed by their index. A halt belongs to one call (Train::serviceStopOf), so a train that visits a station twice
// marks only the call it stands at as stood at.
int advanceFirstStop(const Train& train, int from) {
	const int count = stopCount(train);
	if (train.OutOfSimulation)
		return count;
	int stop = max(0, from);
	while (stop < count) {
		const bool stoppedHere = train.Stations[static_cast<size_t>(stop)].StepStopped > 0;
		const bool stoppedLater = stop + 1 < count && train.Stations[static_cast<size_t>(stop) + 1].StepStopped > 0;
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

TrainState liveTrainState(const Train& train, int firstStop, int elapsed) {
	TrainState state;
	const int count = stopCount(train);
	state.firstStop = static_cast<size_t>(max(0, firstStop));
	bool arrived = false;
	for (int stop = min(firstStop, count - 1); stop >= 0 && !arrived; --stop) {
		const size_t at = static_cast<size_t>(stop);
		if (train.StationArrivals[at] >= 0 && train.ScheduledArrivals[at] >= 0) {
			state.delay = max(0.0, train.StationArrivals[at] - train.ScheduledArrivals[at]);
			arrived = true;
		}
	}
	// Before its first arrival, a train that entered later than it could is as late as its entry.
	if (!arrived && train.CanEnter && train.earliestActiveTrajectoryIndex >= 0)
		state.delay = max(0.0, train.earliestActiveTrajectoryIndex * timestep - train.departure_time);
	if (firstStop < count && train.StoppedForServiceStop && train.Stations[static_cast<size_t>(firstStop)].StepStopped > 0) {
		const double planned = train.ScheduledDepartures[static_cast<size_t>(firstStop)];
		if (planned >= 0 && planned + state.delay < elapsed)
			state.delay = elapsed - planned;
	}
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
		states_[static_cast<size_t>(n)] = liveTrainState(train, watch.firstStop, t);
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
	for (int s = static_cast<int>(states_[static_cast<size_t>(train)].firstStop); s < stopCount(T); ++s)
		if (haltsAtStop(T, s) && T.Stations[static_cast<size_t>(s)].stationName == station
			&& T.Stations[static_cast<size_t>(s)].stationPlatformId == platformId)
			return true;
	return false;
}

Plan PassengerJourneys::planFrom(const SimulationRun& run, const Journey& journey, const string& origin, int t, int readyAt, int platformNumber) {
	const int from = stationIndex(origin);
	const int to = stationIndex(journey.Arr_Station_ID);
	if (from < 0 || to < 0)
		return Plan();
	ensureStates(run, t);
	const int transfers = run.settings.passengerMaxTransfers;
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
	const auto key = make_tuple(from, readyAt, platformNumber);
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
	const Plan plan = given != nullptr ? *given : planFrom(run, journey, station, t, readyAt, platformNumber);
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
	int readyAt = now;
	int platformNumber = alightedAtPlatform.value_or(0);
	if (kept < journey->Trips.size()) {
		const Trip& given = *next(journey->Trips.begin(), static_cast<ptrdiff_t>(kept));
		// Second 0 of the day is a time; a trip that nothing has started has -9999.
		if (given.IsTripStarted && given.Actual_Departure_Time >= 0) {
			if (alightedAtPlatform) {
				// The passenger has just alighted and has not set off for the platform of the train that is given up, so the
				// walk there that the transfer counted is not made: the plan starts now, on the platform of the arrival.
				journey->Walkingtime -= max(0, given.Actual_Departure_Time - now);
			} else {
				// The time spent waiting for the train that is given up still counts as waiting.
				if (now > given.Actual_Departure_Time)
					journey->Waitingtime += now - given.Actual_Departure_Time;
				// A passenger still walking to the platform cannot leave before getting there.
				readyAt = max(now, given.Actual_Departure_Time);
				platformNumber = given.depPlatformNumber;
			}
		}
	}
	planInto(run, passenger, *journey, kept, t, readyAt, platformNumber, true, given);
}

optional<Plan> PassengerJourneys::standingDirectPlan(const SimulationRun& run, const Passenger& passenger, const Journey& journey,
	const Trip& current, int t, optional<int> alightedAtPlatform) const {
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
	// When the plan reaches the destination: the planned arrival of its last train and the delay that train has now.
	const Train& last = run.trains[static_cast<size_t>(lastTrain)];
	double planArrival = passenger_routing::kUnreached;
	for (int s = static_cast<int>(states_[static_cast<size_t>(lastTrain)].firstStop); s < stopCount(last); ++s)
		if (last.Stations[static_cast<size_t>(s)].stationName == journey.Arr_Station_ID && last.ScheduledArrivals[s] >= 0) {
			planArrival = last.ScheduledArrivals[s] + start + states_[static_cast<size_t>(lastTrain)].delay;
			break;
		}
	if (planArrival == passenger_routing::kUnreached)
		return nullopt;
	optional<Plan> best;
	for (int n = 0; n < run.numTrains; ++n) {
		const Train& T = run.trains[static_cast<size_t>(n)];
		// Only a train that stands at the platform now.
		if (n == currentTrain || n == leftTrain || !T.StoppedForServiceStop || T.OutOfSimulation || T.CurrentServiceStop != station
			|| T.CurrentServiceStopPlatform != platformId)
			continue;
		const TrainState& state = states_[static_cast<size_t>(n)];
		const size_t board = state.firstStop;
		if (board >= static_cast<size_t>(stopCount(T)) || T.Stations[board].stationName != station
			|| T.Stations[board].stationPlatformId != platformId || T.ScheduledDepartures[board] < 0 || !haltsAtStop(T, static_cast<int>(board)))
			continue;
		const double departure = T.ScheduledDepartures[board] + start + state.delay;
		const double walking = passenger_routing::walkingSeconds(alightedAtPlatform.value_or(0), T.stationPlatformNumber[board], run.settings.passengerWalkingSecondsPerPlatform);
		if (t + start + walking > departure)
			continue;
		for (size_t s = board + 1; s < static_cast<size_t>(stopCount(T)); ++s) {
			if (T.Stations[s].stationName != journey.Arr_Station_ID || T.ScheduledArrivals[s] < 0)
				continue;
			const double arrival = T.ScheduledArrivals[s] + start + state.delay;
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
		replanWaiting(run, passenger, t, alightedAtPlatform);
	} else if (const optional<Plan> direct = standingDirectPlan(run, passenger, *journey, *trip, t, alightedAtPlatform)) {
		// A train that stands here and goes direct to the destination in time is taken instead of the plan.
		replanWaiting(run, passenger, t, alightedAtPlatform, &*direct);
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
	for (; watch.handled < watch.firstStop; ++watch.handled) {
		const Node& stop = T.Stations[static_cast<size_t>(watch.handled)];
		for (const int position : flow.waitingFor(flow.platformNumber(stop.stationName, stop.stationPlatformId), trainId)) {
			replanWaiting(run, flow.passenger(position), t);
			flow.alighted(position);
		}
	}
}

void PassengerJourneys::setDownEarly(SimulationRun& run, Passenger& passenger, const Train& train, int stop, int t) {
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
	trip->Arr_Station_ID = node.stationName;
	trip->Arr_Station_Platform_ID = node.stationPlatformId;
	trip->Actual_Arrival_Time = now;
	if (train.ScheduledArrivals[stop] >= 0)
		trip->Planned_Arrival_Time = roundedSeconds(train.ScheduledArrivals[stop] + run.settings.startingSimulationTime);
	if (trip->Planned_Arrival_Time != -9999)
		trip->totalArrivalDelay = trip->Actual_Arrival_Time - trip->Planned_Arrival_Time;
	trip->IsTripCompleted = true;
	passenger.CurrentStatus = PassengerStatus::OnPlatform;
	passenger.current_location_ID = passenger.Current_WaitingStationID = node.stationName;
	passenger.Current_WaitingStationPlatformID = node.stationPlatformId;
	passenger.Current_Train_Boarded = passenger.Current_Train_To_Wait = kNoneId;
	planInto(run, passenger, *journey, static_cast<size_t>(distance(journey->Trips.begin(), trip)) + 1, t, now,
		train.stationPlatformNumber[static_cast<size_t>(stop)], true);
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

void PassengerJourneys::settleTrains(SimulationRun& run, PassengerFlow& flow, int t) {
	beginPhase();
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
	}
}
