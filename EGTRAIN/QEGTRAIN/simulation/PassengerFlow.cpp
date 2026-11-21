#include "simulation/PassengerFlow.h"
#include "simulation/PassengerJourneys.h"
#include "simulation/PassengerRouting.h"
#include "simulation/Simulation.h"
#include "simulation/SimulationRun.h"
#include "util/Log.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <optional>

using namespace std;

namespace {
bool isWaitingOnPlatform(const Passenger& p) {
	return p.IsIntheNetwork && p.CurrentStatus == PassengerStatus::OnPlatform;
}

// Calls emit(departureTime) for each trip that the waiting passenger waits for: the active trip of each journey with the ID of the
// active journey. It is the time from which the passenger waits for the train of the trip.
template <class Emit>
void forEachWaitingTrip(const Passenger& p, Emit&& emit) {
	for (const Journey& journey : p.Journeys) {
		if (journey.ID != p.current_JourneyID)
			continue;
		const auto activetrip = std::find_if(journey.Trips.begin(), journey.Trips.end(),
			[&p](const Trip& trip) { return trip.TripID == p.current_TripID; });
		if (activetrip != journey.Trips.end())
			emit(activetrip->Actual_Departure_Time);
	}
}

// The arrival delay of a trip that ended: its arrival less its planned arrival, or 0 when the timetable has no planned arrival at the
// stop where it ended.
int tripArrivalDelay(const Trip& trip) {
	return trip.Planned_Arrival_Time == -9999 ? 0 : trip.Actual_Arrival_Time - trip.Planned_Arrival_Time;
}

// Counts a passenger who waits on a platform and adds the pair of the passenger ID and the actual departure time of the trip
// to wait for to the list of the platform
void addWaitingPassenger(const Passenger& p, WaitingAtPlatform& waiting) {
	waiting.count++;
	forEachWaitingTrip(p, [&](double since) { waiting.passengers.emplace_back(p.ID, since); });
}

// The time of the first journey of the passenger to start that is not completed, or none. The passenger enters the network when
// the time has come.
std::optional<double> nextJourneyStart(const Passenger& p) {
	std::optional<double> earliest;
	for (const Journey& journey : p.Journeys) {
		if (!journey.isFinished() && (!earliest || journey.Actual_Planned_Departure_Time < *earliest))
			earliest = journey.Actual_Planned_Departure_Time;
	}
	return earliest;
}

std::uint64_t pairKey(int first, int second) {
	return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(first)) << 32) | static_cast<std::uint32_t>(second);
}
} // namespace

// Function to check the start of the Journey (hence check their entrance on the network) for all simulated passengers
void checkJourneyStartForAllPassengers(int t, int StartingSimulationTime, list<Passenger>& SIMUL_PAX) {
	for (Passenger& p : SIMUL_PAX)
		p.checkJourneyStart(t + StartingSimulationTime);
}

WaitingAtPlatform passengersWaitingAtPlatform(const string& stationId, const string& platformId, const list<Passenger>& ALL_PAX) {
	WaitingAtPlatform waiting;
	for (const Passenger& p : ALL_PAX) {
		if (isWaitingOnPlatform(p) && p.Current_WaitingStationID == stationId && p.Current_WaitingStationPlatformID == platformId)
			addWaitingPassenger(p, waiting);
	}
	// Order the list of passengers waiting at the platform in chronological order of their actual departure time
	waiting.passengers.sort(orderPassengerListOnPlatform);
	return waiting;
}

void Update_List_Passengers_Waiting_At_ALL_Platforms(list<StationPlatform>& ALL_PLAT, const list<Passenger>& ALL_PAX) {
	map<pair<string, string>, WaitingAtPlatform> waitingAt; // by station ID and platform ID
	for (const Passenger& p : ALL_PAX) {
		if (isWaitingOnPlatform(p))
			addWaitingPassenger(p, waitingAt[{p.Current_WaitingStationID, p.Current_WaitingStationPlatformID}]);
	}
	for (auto& platformWaiting : waitingAt)
		platformWaiting.second.passengers.sort(orderPassengerListOnPlatform);

	const WaitingAtPlatform nobody;
	for (StationPlatform& plat : ALL_PLAT) {
		const auto found = waitingAt.find({plat.StationID, plat.ID});
		const WaitingAtPlatform& waiting = found == waitingAt.end() ? nobody : found->second;
		plat.Current_N_Passengers = waiting.count;
		plat.Current_List_Pax_On_Platform = waiting.passengers;
	}
}

bool PassengerFlow::Waiter::operator<(const Waiter& other) const {
	if (since < other.since)
		return true;
	if (other.since < since)
		return false;
	return position != other.position ? position < other.position : leg < other.leg;
}

int PassengerFlow::idOf(const string& name) {
	return ids_.try_emplace(name, static_cast<int>(ids_.size())).first->second;
}

int PassengerFlow::findId(const string& name) const {
	const auto found = ids_.find(name);
	return found == ids_.end() ? -1 : found->second;
}

int PassengerFlow::platformKey(const string& stationId, const string& platformId) {
	const auto added = platformKeys_.try_emplace({stationId, platformId}, static_cast<int>(queues_.size()));
	if (added.second)
		queues_.emplace_back();
	return added.first->second;
}

int PassengerFlow::findPlatformKey(const string& stationId, const string& platformId) const {
	const auto found = platformKeys_.find({stationId, platformId});
	return found == platformKeys_.end() ? -1 : found->second;
}

void PassengerFlow::clear() {
	*this = PassengerFlow();
}

void PassengerFlow::build(list<Passenger>& passengers, bool trackJourneyStarts) {
	clear();
	source_ = &passengers;
	count_ = passengers.size();
	trackJourneyStarts_ = trackJourneyStarts;
	passengers_.reserve(count_);
	for (Passenger& passenger : passengers)
		passengers_.push_back(&passenger);
	registrations_.resize(count_);
	for (int position = 0; position < static_cast<int>(count_); ++position) {
		registerState(position);
		if (trackJourneyStarts && !passengers_[static_cast<std::size_t>(position)]->IsIntheNetwork) {
			if (const auto due = nextJourneyStart(*passengers_[static_cast<std::size_t>(position)]))
				journeyStarts_.push_back({*due, position});
		}
	}
	std::make_heap(journeyStarts_.begin(), journeyStarts_.end(), std::greater<JourneyStart>());
}

bool PassengerFlow::covers(const list<Passenger>& passengers) const {
	return source_ == &passengers && count_ == passengers.size();
}

void PassengerFlow::markDirty(int platform) {
	PlatformQueues& queue = queues_[static_cast<std::size_t>(platform)];
	if (!queue.dirty) {
		queue.dirty = true;
		dirtyPlatforms_.push_back(platform);
	}
}

void PassengerFlow::registerState(int position) {
	const Passenger& p = *passengers_[static_cast<std::size_t>(position)];
	Registration& registration = registrations_[static_cast<std::size_t>(position)];
	if (isWaitingOnPlatform(p)) {
		registration.kind = Kind::Waiting;
		registration.first = platformKey(p.Current_WaitingStationID, p.Current_WaitingStationPlatformID);
		registration.second = idOf(p.Current_Train_To_Wait);
		PlatformQueues& queue = queues_[static_cast<std::size_t>(registration.first)];
		const auto known = queue.byTrain.find(registration.second);
		registration.refused = p.TrainThatRefusedBoarding == p.Current_Train_To_Wait
			&& p.StopThatRefusedBoarding == (known == queue.byTrain.end() ? -1 : known->second.call);
		queue.count++;
		int leg = 0;
		forEachWaitingTrip(p, [&](double since) {
			registration.waiters.push_back({since, position, leg++});
			TrainQueue& line = queue.byTrain[registration.second];
			line.all.insert(registration.waiters.back());
			if (!registration.refused)
				line.notRefused.insert(registration.waiters.back());
		});
		markDirty(registration.first);
	} else if (p.CurrentStatus == PassengerStatus::OnBoard) {
		registration.kind = Kind::OnBoard;
		registration.first = idOf(p.Current_Train_Boarded);
		registration.second = idOf(p.Current_Arrival_Station);
		onBoard_[pairKey(registration.first, registration.second)].push_back(position);
	}
}

void PassengerFlow::unregister(int position) {
	Registration& registration = registrations_[static_cast<std::size_t>(position)];
	if (registration.kind == Kind::Waiting) {
		PlatformQueues& queue = queues_[static_cast<std::size_t>(registration.first)];
		queue.count--;
		// A passenger whose journey has no trip for the train is counted on the platform but has no place in a queue.
		const auto waiting = queue.byTrain.find(registration.second);
		if (waiting != queue.byTrain.end()) {
			for (const Waiter& waiter : registration.waiters) {
				waiting->second.all.erase(waiter);
				waiting->second.notRefused.erase(waiter);
			}
			if (waiting->second.all.empty())
				queue.byTrain.erase(waiting);
		}
		markDirty(registration.first);
	} else if (registration.kind == Kind::OnBoard) {
		std::vector<int>& bucket = onBoard_[pairKey(registration.first, registration.second)];
		bucket.erase(std::find(bucket.begin(), bucket.end(), position));
	}
	registration = Registration();
}

void PassengerFlow::placed(int position) {
	unregister(position);
	registerState(position);
}

void PassengerFlow::scheduleJourneyStart(int position) {
	if (!trackJourneyStarts_)
		return;
	const Passenger& p = *passengers_[static_cast<std::size_t>(position)];
	if (p.IsIntheNetwork)
		return;
	if (const auto due = nextJourneyStart(p)) {
		journeyStarts_.push_back({*due, position});
		std::push_heap(journeyStarts_.begin(), journeyStarts_.end(), std::greater<JourneyStart>());
	}
}

void PassengerFlow::alighted(int position) {
	placed(position);
	scheduleJourneyStart(position);
}

void PassengerFlow::startDueJourneys(int now, const std::function<void(int position)>& onStarted) {
	lastTime_ = now;
	while (!journeyStarts_.empty() && journeyStarts_.front().due <= now) {
		const int position = journeyStarts_.front().position;
		std::pop_heap(journeyStarts_.begin(), journeyStarts_.end(), std::greater<JourneyStart>());
		journeyStarts_.pop_back();
		Passenger& passenger = *passengers_[static_cast<std::size_t>(position)];
		passenger.checkJourneyStart(now);
		if (onStarted && passenger.IsIntheNetwork)
			onStarted(position);
		placed(position);
		// A passenger whose journey found no route is outside the network again, with the next journey to start.
		if (!passenger.IsIntheNetwork)
			scheduleJourneyStart(position);
	}
}

int PassengerFlow::waitingCount(const string& stationId, const string& platformId) const {
	const int key = findPlatformKey(stationId, platformId);
	return key < 0 ? 0 : queues_[static_cast<std::size_t>(key)].count;
}

std::optional<PassengerFlow::Waiter> PassengerFlow::nextWaiter(int platform, int train, const std::optional<Waiter>& after, bool onlyNotRefused) const {
	if (platform < 0 || train < 0)
		return std::nullopt;
	const PlatformQueues& queue = queues_[static_cast<std::size_t>(platform)];
	const auto line = queue.byTrain.find(train);
	if (line == queue.byTrain.end())
		return std::nullopt;
	const std::set<Waiter>& waiting = onlyNotRefused ? line->second.notRefused : line->second.all;
	const auto next = after ? waiting.upper_bound(*after) : waiting.begin();
	return next == waiting.end() ? std::nullopt : std::optional<Waiter>(*next);
}

void PassengerFlow::refused(int position) {
	Registration& registration = registrations_[static_cast<std::size_t>(position)];
	registration.refused = true;
	if (registration.kind != Kind::Waiting)
		return;
	const auto line = queues_[static_cast<std::size_t>(registration.first)].byTrain.find(registration.second);
	for (const Waiter& waiter : registration.waiters)
		line->second.notRefused.erase(waiter);
}

void PassengerFlow::beginCall(int platform, int train, int call) {
	if (platform < 0 || train < 0)
		return;
	const auto line = queues_[static_cast<std::size_t>(platform)].byTrain.find(train);
	if (line == queues_[static_cast<std::size_t>(platform)].byTrain.end() || line->second.call == call)
		return;
	line->second.call = call;
	for (const Waiter& waiter : line->second.all) {
		registrations_[static_cast<std::size_t>(waiter.position)].refused = false;
		line->second.notRefused.insert(waiter);
	}
}

std::vector<int> PassengerFlow::waitingFor(int platform, int train) const {
	std::vector<int> positions;
	if (platform < 0 || train < 0)
		return positions;
	const PlatformQueues& queue = queues_[static_cast<std::size_t>(platform)];
	const auto line = queue.byTrain.find(train);
	if (line == queue.byTrain.end())
		return positions;
	std::set<int> seen;
	for (const Waiter& waiter : line->second.all)
		if (seen.insert(waiter.position).second)
			positions.push_back(waiter.position);
	return positions;
}

std::vector<int> PassengerFlow::takeAllOnBoard(const string& train) {
	std::vector<int> positions;
	const int trainId = findId(train);
	if (trainId < 0)
		return positions;
	for (auto bucket = onBoard_.begin(); bucket != onBoard_.end();) {
		if (static_cast<std::uint32_t>(bucket->first >> 32) != static_cast<std::uint32_t>(trainId)) {
			++bucket;
			continue;
		}
		positions.insert(positions.end(), bucket->second.begin(), bucket->second.end());
		bucket = onBoard_.erase(bucket);
	}
	for (const int position : positions)
		registrations_[static_cast<std::size_t>(position)] = Registration();
	std::sort(positions.begin(), positions.end());
	return positions;
}

std::vector<int> PassengerFlow::takeOnBoardArrivingAt(const string& train, const string& station) {
	const int trainId = findId(train);
	const int stationId = findId(station);
	if (trainId < 0 || stationId < 0)
		return {};
	const auto found = onBoard_.find(pairKey(trainId, stationId));
	if (found == onBoard_.end())
		return {};
	std::vector<int> positions = std::move(found->second);
	onBoard_.erase(found);
	for (const int position : positions)
		registrations_[static_cast<std::size_t>(position)] = Registration();
	std::sort(positions.begin(), positions.end());
	return positions;
}

void PassengerFlow::ensureIdPositions() {
	if (!sameId_.empty() || passengers_.empty())
		return;
	for (int position = 0; position < static_cast<int>(passengers_.size()); ++position)
		byId_[passengers_[static_cast<std::size_t>(position)]->ID].push_back(position);
	sameId_.reserve(passengers_.size());
	for (const Passenger* p : passengers_)
		sameId_.push_back(&byId_[p->ID]);
}

const std::vector<int>& PassengerFlow::samePassengerId(int position) {
	ensureIdPositions();
	return *sameId_[static_cast<std::size_t>(position)];
}

const std::vector<int>& PassengerFlow::passengersWithId(const string& id) {
	static const std::vector<int> none;
	ensureIdPositions();
	const auto found = byId_.find(id);
	return found == byId_.end() ? none : found->second;
}

WaitingAtPlatform PassengerFlow::listFor(int platform) const {
	const PlatformQueues& queue = queues_[static_cast<std::size_t>(platform)];
	WaitingAtPlatform waiting;
	waiting.count = queue.count;
	std::vector<Waiter> everyone;
	for (const auto& train : queue.byTrain)
		everyone.insert(everyone.end(), train.second.all.begin(), train.second.all.end());
	std::sort(everyone.begin(), everyone.end());
	for (const Waiter& waiter : everyone)
		waiting.passengers.emplace_back(passengers_[static_cast<std::size_t>(waiter.position)]->ID, waiter.since);
	return waiting;
}

WaitingAtPlatform PassengerFlow::waitingAt(const string& stationId, const string& platformId) const {
	const int key = findPlatformKey(stationId, platformId);
	return key < 0 ? WaitingAtPlatform() : listFor(key);
}

void PassengerFlow::attachPlatforms(list<StationPlatform>& platforms) {
	platformsSource_ = &platforms;
	platformsCount_ = platforms.size();
	allPlatforms_.clear();
	platformsByKey_.clear();
	for (StationPlatform& platform : platforms) {
		allPlatforms_.push_back(&platform);
		const int key = platformKey(platform.StationID, platform.ID);
		if (static_cast<std::size_t>(key) >= platformsByKey_.size())
			platformsByKey_.resize(static_cast<std::size_t>(key) + 1);
		platformsByKey_[static_cast<std::size_t>(key)].push_back(&platform);
	}
	allPlatformsStale_ = true;
}

void PassengerFlow::refreshPlatformLists(list<StationPlatform>& platforms) {
	if (platformsSource_ != &platforms || platformsCount_ != platforms.size())
		attachPlatforms(platforms);
	const auto assign = [this](StationPlatform& platform, int key) {
		const WaitingAtPlatform waiting = listFor(key);
		platform.Current_N_Passengers = waiting.count;
		platform.Current_List_Pax_On_Platform = waiting.passengers;
	};
	if (allPlatformsStale_) {
		for (StationPlatform* platform : allPlatforms_)
			assign(*platform, findPlatformKey(platform->StationID, platform->ID));
		allPlatformsStale_ = false;
	} else {
		for (const int key : dirtyPlatforms_) {
			if (static_cast<std::size_t>(key) < platformsByKey_.size())
				for (StationPlatform* platform : platformsByKey_[static_cast<std::size_t>(key)])
					assign(*platform, key);
		}
	}
	for (const int key : dirtyPlatforms_)
		queues_[static_cast<std::size_t>(key)].dirty = false;
	dirtyPlatforms_.clear();
}

namespace {
// The passenger p, who is on board of train T, alights at the service stop of T: the trip ends, and the passenger leaves the
// network or waits for the next trip.
void alightPassenger(Passenger& passenger, Train& T, int t, int SimulationStartingTime) {
	Passenger* p = &passenger;

	// Update travel information of the alighted passenger
	if (p->Journeys.empty() != 1) {

		for (list<Journey>::iterator j = p->Journeys.begin(); j != p->Journeys.end(); j++) {
			if ((j->ID == p->current_JourneyID) && (j->IsJourneyCompleted == 0)) { // Select the current journey of the passenger if it is not yet completed

				// Defining iterator of the current, the next trip and the last trips of the active journey

				list<Trip>::iterator currenttrip = j->Trips.begin();
				list<Trip>::iterator nexttrip = j->Trips.begin();
				nexttrip++; // if the journey is composed of one signle trip then next trip will concide with Trips.end() iterator
				list<Trip>::iterator LasttripOfJourney = j->Trips.end();
				LasttripOfJourney--; // Selecting the last trip of the active journey list

				// iterate throught the active trips
				while (currenttrip != j->Trips.end()) {
					// if the active trip is found and has the arrival stop where the train T is stopping at
					if ((currenttrip->TripID == p->current_TripID) && (currenttrip->Arr_Station_ID == T.CurrentServiceStop)) {
						// then set the trip as complete and let the passenger get on the platform
						p->current_location_ID = T.CurrentServiceStop;
						currenttrip->Actual_Arrival_Time = t + SimulationStartingTime;
						currenttrip->totalArrivalDelay = tripArrivalDelay(*currenttrip);
						currenttrip->IsTripCompleted = true;

						// if the selected trip is the last trip of the active journey then set that journey j will be completed
						if ((currenttrip->TripID == LasttripOfJourney->TripID) && (currenttrip->Arr_Station_ID == j->Arr_Station_ID)) {
							j->Actual_Arrival_Time = currenttrip->Actual_Arrival_Time;
							j->totalJourneyArrivalDelay = currenttrip->totalArrivalDelay; // Could also be computed as Actual_Arrival_Time - Actual_Planned_Arrival_Time
							// however it might be that in the rescheduling process the Planned arrival time of the last trip of the journey is changed based on rerouting or replanning of the train service.

							j->IsJourneyCompleted = true;
							j->Outcome = JourneyOutcome::Completed;
							p->current_location_ID = T.CurrentServiceStop;
							p->IsIntheNetwork = false; // The passenger is selected and sent out of the simulation as the journey is ended
							p->TimeExitedTheNetwork = currenttrip->Actual_Arrival_Time;
							p->StationExitedTheNetworkID = currenttrip->Arr_Station_ID;
							p->PlatformExitedTheNetworkID = currenttrip->Arr_Station_Platform_ID;
							p->CurrentStatus = PassengerStatus::Outside;
							p->current_TripID = kNoneId;
							p->current_JourneyID = kNoneId;
							p->Current_Train_Boarded = kNoneId;
							p->Current_Arrival_Station = kNoneId;
							p->Current_Arrival_Platform = kNoneId;

						}

						else { // else if the selected trip is not the last trip of the journey then it means that the passenger will need to transfer and take another trip to complete its journey
							// computing the actual and planned arrival times of the current trip as well as its Total arrival delay
							currenttrip->Actual_Arrival_Time = t + SimulationStartingTime;
							currenttrip->totalArrivalDelay = tripArrivalDelay(*currenttrip);
							currenttrip->IsTripCompleted = true;

							p->current_TripID = nexttrip->TripID;
							p->CurrentStatus = PassengerStatus::OnPlatform;
							p->Current_WaitingStationID = T.CurrentServiceStop;
							p->current_location_ID = T.CurrentServiceStop;
							// The passenger waits for the next train on the platform where that train stops, or on the
							// arrival platform when the next trip has none.
							p->Current_WaitingStationPlatformID = !isNoneId(nexttrip->Dep_Station_Platform_ID)
								? nexttrip->Dep_Station_Platform_ID
								: T.CurrentServiceStopPlatform;
							p->Current_Train_Boarded = kNoneId;
							p->Current_Train_To_Wait = nexttrip->TrainServiceDescription;
							p->Current_Arrival_Station = nexttrip->Arr_Station_ID;
							p->Current_Arrival_Platform = nexttrip->Arr_Station_Platform_ID;

							// check that the waiting station of the trip is the same as the one departure station of teh nexttripID
							if (p->Current_WaitingStationID != nexttrip->Dep_Station_ID) {
								EG_LOG(SimPax, Warning) << "Warning: Passenger " << p->ID << "will depart from a station different than the one planned for active trip " << p->current_TripID << " in journey " << p->current_JourneyID << "\n";
							} else {
								// if the arrival station of teh previous trip is the same as the departure station of the next trip then check whether the passenger is waiting at the scheduled platform
								// Set first of all that the next trip has started
								nexttrip->IsTripStarted = true;
								// The walk between the platforms is the one the routing plans (an unknown platform number needs none).
								const double Walkingtime = passenger_routing::walkingSeconds(T.currentServiceStopPlatformNumber, nexttrip->depPlatformNumber);
								nexttrip->Actual_Departure_Time = t + SimulationStartingTime + Walkingtime;
								j->Walkingtime = j->Walkingtime + Walkingtime;
							}
						}
						break; // break the while loop on trips
					}
					currenttrip++;
					// Advance the iterator nexttrip only if the next current trip is not the last trip of the journey, otherwise an exeption is thrown because of exceeding elements in the trip list
					if (currenttrip != LasttripOfJourney) {
						nexttrip++;
					}
				}
				break; // break the for loop over the journeys once the active one has been identified
			}
		}
	}
}

// Lets the passengers on board of T whose trip ends at its service stop alight, in the order of the passenger list. Returns their number.
int alightPassengers(PassengerFlow& flow, SimulationRun& run, Train& T, int t, int SimulationStartingTime, PassengerJourneys* journeys) {
	const std::vector<int> alighting = flow.takeOnBoardArrivingAt(T.trainDescription, T.CurrentServiceStop);
	for (const int position : alighting) {
		T.Current_OnBoard_Passengers--; // the passenger leaves the train
		alightPassenger(flow.passenger(position), T, t, SimulationStartingTime);
		// A passenger who transfers plans again when the train of the next trip is already gone, from the platform of this train.
		if (journeys != nullptr)
			journeys->checkWaiting(run, flow.passenger(position), t, T.currentServiceStopPlatformNumber);
		flow.alighted(position);
	}
	return static_cast<int>(alighting.size());
}

// What happened when a passenger tried to board.
enum class Boarding { Nothing,
	Boarded,
	Refused };

// Lets the passenger p board the train T when the passenger waits for it and the train has room. N_BoardedPax counts the passengers
// who boarded at the current service stop, the call at stop `call` of T.
Boarding boardPassenger(Passenger* p, Train& T, int call, int t, int SimulationStartingTime, int& N_BoardedPax) {
	Boarding result = Boarding::Nothing;
	if ((p->CurrentStatus == PassengerStatus::OnPlatform) && (p->Current_Train_To_Wait == T.trainDescription) && (p->Current_WaitingStationID == T.CurrentServiceStop)) {
		// Let the passenger having as train to wait the current train T and Waiting station the currentService Stop board the train
		// passengers can board only if the number of onboard passengers is lower than the max capacity
		if (T.Current_OnBoard_Passengers < T.MAX_OnBoard_Passengers) {
			N_BoardedPax++;
			result = Boarding::Boarded;
			T.Current_OnBoard_Passengers++;
			p->CurrentStatus = PassengerStatus::OnBoard;
			p->Current_Train_Boarded = T.trainDescription;
			p->Current_WaitingStationID = kNoneId;
			p->Current_WaitingStationPlatformID = kNoneId;
			p->Current_Train_To_Wait = kNoneId;

			if (p->Journeys.empty() != 1) {
				for (list<Journey>::iterator j = p->Journeys.begin(); j != p->Journeys.end(); j++) {
					if ((j->ID == p->current_JourneyID) && (j->IsJourneyCompleted == 0)) {
						list<Trip>::iterator currenttrip = j->Trips.begin();
						while (currenttrip != j->Trips.end()) {
							if (currenttrip->TripID == p->current_TripID) {

								currenttrip->Actual_Boarding_Time = t + SimulationStartingTime;
								int Waiting_Time = t + SimulationStartingTime - currenttrip->Actual_Departure_Time;
								j->Waitingtime = j->Waitingtime + Waiting_Time;
								break; // when the waiting time has been set for the correct journey and trip it is possible to break the while loop over the trips in the active journey
							}
							currenttrip++; // advance the current trip iterator by one.
						}
						break; // the active journey has been found
					}
				}
			}

		}

		else if (p->TrainThatRefusedBoarding != T.trainDescription || p->StopThatRefusedBoarding != call) {
			// The train is full. The passenger keeps waiting for it; the refusal is logged once per call of the train.
			p->TrainThatRefusedBoarding = T.trainDescription;
			p->StopThatRefusedBoarding = call;
			result = Boarding::Refused;
			EG_LOG(SimPax, Info) << "Passenger " << p->ID << " could not board train " << T.trainDescription << " as it was full\n";
		}
	}
	return result;
}

// Registers what the attempt to board did to the passenger at this place.
// A passenger who was refused plans again from this station without the train, when the run plans journeys.
void settleBoarding(PassengerFlow& flow, SimulationRun& run, Train& T, int t, PassengerJourneys* journeys, int position, Boarding result, StopCallCounts* counts) {
	if (result == Boarding::Boarded) {
		flow.placed(position);
	} else if (result == Boarding::Refused) {
		if (counts != nullptr)
			++counts->leftBehind;
		flow.refused(position);
		if (journeys != nullptr) {
			journeys->replanRefused(run, flow.passenger(position), T, t);
			flow.alighted(position);
		}
	}
}

// The platform where the train stops, or null.
const StationPlatform* platformOfStop(const SimulationRun& run, const Train& T, const list<StationPlatform>& platforms,
	std::optional<PlatformIndex>& own) {
	if (run.platformIndex.covers(platforms))
		return run.platformIndex.find(T.CurrentServiceStop, T.CurrentServiceStopPlatform);
	own.emplace();
	own->build(platforms);
	return own->find(T.CurrentServiceStop, T.CurrentServiceStopPlatform);
}

// The counts of the call that train T makes at its current stop, or null when T is not a train of the run or the stop is not found.
// The stop is the call that the train has stood at and not left (Train::serviceStopOf).
StopCallCounts* currentCall(SimulationRun& run, const Train& T) {
	const Train* first = run.trains.data();
	if (&T < first || &T >= first + run.trains.size())
		return nullptr;
	const int stop = T.serviceStopOf(T.CurrentServiceStop);
	if (stop >= 0 && stop < Train::kMaxTimetableStations && T.Stations[static_cast<size_t>(stop)].StepStopped > 0 && T.StationDepartures[stop] < 0)
		return &run.stopCalls.at(static_cast<int>(&T - first), stop);
	return nullptr;
}

// The boarding procedure follows the order at which passengers arrived at the platform. The platform occupation rate, the pax
// of the current platform plus those just alighted from the train, is returned in occupationRate.
int boardPassengers(PassengerFlow& flow, SimulationRun& run, Train& T, int t, int SimulationStartingTime,
	const list<StationPlatform>& platforms, int N_AlightPax, double& occupationRate, PassengerJourneys* journeys, StopCallCounts* counts) {
	int N_BoardedPax = 0;
	std::optional<PlatformIndex> ownPlatforms;
	const StationPlatform* PlatformIt = platformOfStop(run, T, platforms, ownPlatforms);
	if (PlatformIt == nullptr)
		return N_BoardedPax;
	const int now = t + SimulationStartingTime;
	const int call = T.serviceStopOf(T.CurrentServiceStop);
	if (run.settings.PAX_GUI) {
		// With the passenger GUI the count and the list of the waiting passengers of the platform are updated at every time instant,
		// and the train boards from that list.
		occupationRate = passengerOccupancyRatio(PlatformIt->Current_N_Passengers + N_AlightPax, PlatformIt->Max_Passenger_Volume);
		for (const pair<string, double>& Pax : PlatformIt->Current_List_Pax_On_Platform) {
			// A passenger who still walks from another platform after a transfer is not on this platform yet.
			if (Pax.second > now)
				continue;
			// the passengers with the ID of the one in the list of passengers on the platform, in the order of the list of all passengers
			for (const int position : flow.passengersWithId(Pax.first))
				settleBoarding(flow, run, T, t, journeys, position, boardPassenger(&flow.passenger(position), T, call, t, SimulationStartingTime, N_BoardedPax), counts);
		}
		return N_BoardedPax;
	}
	// Without it the passengers waiting for this train are taken from the flow, and the platform is not modified.
	occupationRate = passengerOccupancyRatio(flow.waitingCount(PlatformIt->StationID, PlatformIt->ID) + N_AlightPax, PlatformIt->Max_Passenger_Volume);
	const int platform = flow.platformNumber(PlatformIt->StationID, PlatformIt->ID);
	const int train = flow.trainNumber(T.trainDescription);
	flow.beginCall(platform, train, call);
	std::optional<PassengerFlow::Waiter> last;
	while (true) {
		// A full train only refuses the passengers who were not refused yet.
		const bool full = T.Current_OnBoard_Passengers >= T.MAX_OnBoard_Passengers;
		last = flow.nextWaiter(platform, train, last, full);
		if (!last || last->since > now)
			break; // the waiters are in order of time, so the rest still walk
		for (const int position : flow.samePassengerId(last->position))
			settleBoarding(flow, run, T, t, journeys, position, boardPassenger(&flow.passenger(position), T, call, t, SimulationStartingTime, N_BoardedPax), counts);
	}
	return N_BoardedPax;
}

// Changing the dwell time of the train at the station based on the number of alighted and boarded passengers
void applyPassengerDependentDwellTime(Train& T, int N_BoardedPax, int N_AlightPax, double CurrentPlatformOccupationRate) {
	for (int s = 0; s < T.numStations; s++) {
		// Change the Stopping Time of the train at the platform as it arrives based on the Gibson dwell time function
		// The stopTime is changed only when the train first approaches the station (i.e. when StepStopped is = 1) and not later than that
		// This means that the stopping time is not changes if an additional passenger arrives last minute as it is assumed that the platform will be less crowded and the passenger can enter with no additional dwell time required with respect to that necessary to alight / board all passengers when the train approached the platform
		if ((T.Stations[s].stationName == T.CurrentServiceStop) && (T.Stations[s].StepStopped == 1)) {
			// Define variable to set passenger dependendent dwell time based on the Gibson dwell time equation
			double PassengerDependetDwellTime = -1;
			PassengerDependetDwellTime = T.computePaxDependentDwellTimeAtStations(N_BoardedPax, N_AlightPax, CurrentPlatformOccupationRate, T.GibsonDwellTimeParameters[0], T.GibsonDwellTimeParameters[1], T.GibsonDwellTimeParameters[2], T.GibsonDwellTimeParameters[3], T.GibsonDwellTimeParameters[4], T.GibsonDwellTimeParameters[5], T.GibsonDwellTimeParameters[6], T.GibsonDwellTimeParameters[7]);
			// Change the Stop Time of the train at the stopping station only if the Passenger dependent dwell time is larger than the pre-set minimum dwell time at that station / stop
			// In case the passenger dependent well time is lower than the pre-set minimum dwell time, then the minimum dwell time is considered.
			if (PassengerDependetDwellTime > T.Stations[s].StopTime) {

				T.Stations[s].StopTime = PassengerDependetDwellTime;
			}
		}
	}
}

// The passenger step of train T in step t, for the passengers that the flow describes.
void interactAtServiceStop(PassengerFlow& flow, SimulationRun& run, int t, int SimulationStartingTime, Train& T,
	const list<StationPlatform>& platforms, PassengerJourneys* journeys) {
	if (T.StoppedForServiceStop != 1)
		return;
	StopCallCounts* counts = currentCall(run, T);
	const int N_AlightPax = alightPassengers(flow, run, T, t, SimulationStartingTime, journeys);
	double CurrentPlatformOccupationRate = 0; // Occupation rate of the platform where the train is stopping at
	const int N_BoardedPax = boardPassengers(flow, run, T, t, SimulationStartingTime, platforms, N_AlightPax, CurrentPlatformOccupationRate, journeys, counts);
	if (counts != nullptr) {
		counts->alighted += N_AlightPax;
		counts->boarded += N_BoardedPax;
		counts->loadAfter = T.Current_OnBoard_Passengers;
	}
	applyPassengerDependentDwellTime(T, N_BoardedPax, N_AlightPax, CurrentPlatformOccupationRate);
}

// The flow of the run, built again when it does not describe the passengers of the run or the time went back.
PassengerFlow& flowOfRun(SimulationRun& run, int now) {
	if (!run.passengerFlow.covers(run.passengers) || now < run.passengerFlow.lastTime()) {
		run.passengerFlow.build(run.passengers, true);
		run.passengerJourneys.clear();
		run.stopCalls.clear();
	}
	return run.passengerFlow;
}
} // namespace

void Simulate_Train_Passenger_Interactions(SimulationRun& run, int t, int SimulationStartingTime, Train& T,
	list<Passenger>& ALLPAX, const list<StationPlatform>& ALLPLATFORMS) {
	if (T.StoppedForServiceStop != 1)
		return;
	// The passengers and platforms may not be those of the run, or may have been changed by hand, so this call looks at them as they are.
	PassengerFlow flow;
	flow.build(ALLPAX, false);
	interactAtServiceStop(flow, run, t, SimulationStartingTime, T, ALLPLATFORMS, nullptr);
}

void enterPassengersAndRefreshPlatforms(SimulationRun& run, int t) {
	PassengerFlow& flow = flowOfRun(run, t + run.settings.startingSimulationTime);
	// Simulate entrance process of passengers on the railway network according to route choice
	run.passengerJourneys.beginPhase();
	flow.startDueJourneys(t + run.settings.startingSimulationTime,
		[&run, &flow, t](int position) { run.passengerJourneys.startJourney(run, flow.passenger(position), t); });
	// With the passenger GUI the waiting lists of all platforms of the network are updated at every instant
	if (run.settings.PAX_GUI)
		flow.refreshPlatformLists(run.platforms);
}

void simulateTrainPassengerInteractions(SimulationRun& run, int t) {
	PassengerFlow& flow = flowOfRun(run, t + run.settings.startingSimulationTime);
	run.passengerJourneys.settleTrains(run, flow, t);
	// With the passenger GUI, trains board from the platform lists: refresh the platforms that replans or alightings
	// changed, so that a passenger boards in the same step as without the GUI.
	if (run.settings.PAX_GUI)
		flow.refreshPlatformLists(run.platforms);
	for (int n = 0; n < run.numTrains; n++) {
		interactAtServiceStop(flow, run, t, run.settings.startingSimulationTime, run.trains[n], run.platforms, &run.passengerJourneys);
		if (run.settings.PAX_GUI)
			flow.refreshPlatformLists(run.platforms);
	}
	// The load of each train in service after this step, for the peak and average of the run results. It reads the counts
	// of the passenger step above, which runs in every mode, and it changes nothing of the run.
	if (!run.passengers.empty())
		for (int n = 0; n < run.numTrains; n++)
			if (run.trains[n].CanEnter && !run.trains[n].OutOfSimulation)
				run.trains[n].recordOnBoardPassengers();
}
