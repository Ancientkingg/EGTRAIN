#ifndef PASSENGERJOURNEYS_H
#define PASSENGERJOURNEYS_H

#include "simulation/PassengerFlow.h"
#include "simulation/PassengerRouting.h"
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

struct SimulationRun;
class Train;

/// How far a train has come in its stops: the index of the first stop that it has not left. A train that stands at a stop is
/// at that stop; a train that has left the last stop, or that left its route, is at the number of its stops. A stop that the
/// train passed without standing at it is left behind. `from` is the value the earlier call returned, or 0.
int advanceFirstStop(const Train& train, int from);

/// True when the train halts at the stop, so that passengers can board and leave there. A train halts at the end of a stop arc
/// only; a train that enters exactly at the stop (at the start of its route) never does, while a train that is built standing
/// just before its first stop does.
bool haltsAtStop(const Train& train, int stop);

/// Seconds that the train needs under a speed limit of limitMs (m/s) from the first of positions, route positions in metres in
/// ascending order, to each of them; 0 for the first. Each arc of the route runs at the lower of its track speed limit for the train
/// (Train::trackSpeedLimit), the maximum speed of the train and the limit, and a stretch that no arc covers at the limit
/// (passenger_routing::limitedStretchSeconds). One pass over the arcs of the route gives every time.
std::vector<double> limitedRunningSeconds(const SimulationRun& run, const Train& train, const std::vector<double>& positions, double limitMs);

/// What the routing knows about the train at `elapsed` seconds from the start of the run, with firstStop from advanceFirstStop
/// (passenger_routing::TrainProgress, with times on the clock of the day: SimulationRun::settings.startingSimulationTime plus the
/// seconds of the run).
///
/// The delay is the departure delay (Train::StationDepartures) at the latest stop the train reached once it left that stop, else
/// the arrival delay there, and never negative: a train that arrived late can make up the time while it stands. Before the train arrived anywhere
/// it is the lateness of its entry (the step of entering minus Train::departure_time) when it has entered, and 0 when it has not.
/// A train that still stands at its stop after the planned departure plus that delay is at least as late as the time it has stood
/// there, so that a passenger who comes to the platform now can still board it. A train that does not stand at a stop
/// (Train::StoppedForServiceStop, also while it is held at the start of its dwell with its head short of the platform) is at least
/// as late as the time since it was due at its next stop (TrainProgress::nextStop: the first stop on its route at or ahead of where
/// its head was at the step before, and after firstStop and after the last stop where the train stood): due at the planned arrival
/// there, or at the planned departure of a stop without a planned arrival, and at the next stop that has one of them when that stop
/// has neither. So a train that is held between two stops, or that cannot enter its route, gets later while it waits.
///
/// A speed cap of a breakdown that applies to the train now (Active_Train_Breakdown) limits it until the end of the incident. The
/// caps that apply now (Active_Train_Breakdowns) give the limit over time: between two of their ends the lowest of those that still
/// apply, as the simulator takes the lowest cap at every step. A cap that starts later is not foreseen. The running times under a limit (limitedRunningSeconds) are the one to the stop ahead
/// from where the head of the train was at the step before (passengers who enter the network plan before the trains move), or
/// from where it enters when it has not run a step yet, under the cap that applies when it sets off, and those between two later
/// stops on the route of the train that follow each other, as far as the stops come in order along the route.
passenger_routing::TrainProgress liveTrainProgress(const SimulationRun& run, const Train& train, int firstStop, int elapsed);

/// The times on which passengers plan the train at `elapsed`: passenger_routing::predictTrainState of timetable, the timetable of the
/// train in the router, and liveTrainProgress, with SimulationRun::settings.passengerLateThresholdSeconds. The state also says whether
/// the train is late or slowed.
passenger_routing::TrainState liveTrainState(const SimulationRun& run, const Train& train, const passenger_routing::TrainTimetable& timetable,
	int firstStop, int elapsed);

/// What passengers see of the prediction of a train with this timetable (PassengerJourneys::trainsWithNewPrediction compares it from
/// step to step): empty while the train is neither late nor slowed, so that passengers plan it on its planned times. Otherwise it
/// holds whether the train is late, whether it is slowed, the lateness (predicted minus planned time) at the first arrival or
/// departure from TrainState::firstStop on that has both times, in whole minutes rounded down, and for each arrival and departure
/// from TrainState::firstStop on how much later than that first one it is predicted, in whole minutes rounded down (a millisecond
/// less counts as equal), with -1 where it has no planned or no predicted time. While a train is held, at a stop or between two,
/// with or without a speed limit, all its times move by the same second at each step: the first lateness then changes once a
/// minute and the others stay the same, whatever the seconds of the planned times and of the running times under the limit. A
/// change in how the lateness grows along the stops changes it at once. A lateness beyond the range of an int is held to that range,
/// and one that is not a number counts as the largest int.
std::vector<int> predictionSignature(const passenger_routing::TrainTimetable& timetable, const passenger_routing::TrainState& state);

/// The journeys of the passengers that the timetable of the run plans: journeys without legs, and journeys whose legs can no
/// longer be followed. It keeps the router of the run and what the planning needs of the trains, next to the PassengerFlow
/// state in SimulationRun, and is emptied with it.
///
/// A plan is the earliest arrival at the destination from the station where the passenger is now, with at most
/// SimulationRun::settings.passengerMaxTransfers transfers in the whole journey (below), on the times of the trains that liveTrainState gives: the planned times
/// of a train that is at most SimulationRun::settings.passengerLateThresholdSeconds late and has no speed limit, and the predicted
/// times of any other. Its rides become the trips of the journey, named <journey>.live<n>, with the
/// stations and platforms exactly as the stops of the train name them, so that boarding and alighting treat them like trips from
/// the scene. A passenger plans again from the station where it is when it misses its train (the train left, or cannot call
/// there any more), when the train is full, when the train ends its service before the stop where the passenger leaves it, and
/// when the train it waits for runs late (below). A journey with no route ends with JourneyOutcome::NoRoute, and the passenger leaves the network at the station it is at.
/// A missed train after the passenger has ridden a train of the journey is a missed connection (Journey::MissedConnections).
///
/// A passenger who has a plan boards another train that stands at the platform where the passenger waits and reaches the
/// destination without a change no later than the plan does (on the times that liveTrainState gives for both trains, and
/// when it has room); the plan becomes that train. It is looked at when the passenger comes to wait there (the start of a journey
/// with legs, or a transfer) for the trains that stand then, and when a train arrives for the passengers who wait there. The
/// train that the passenger has just left is not another train.
///
/// A passenger who waits for a train that is late or slowed (passenger_routing::TrainState::late and slowed) plans again from where
/// it is each time the prediction of that train changes (predictionSignature): when the train becomes late or slowed, and then
/// whenever its predicted lateness ahead moves to another whole minute, as a departure board shows a delay. A
/// passenger who comes to wait for such a train (the start of a journey with legs, or a transfer) plans again then. The passenger
/// takes the new plan only when it reaches the destination more than kSwitchMarginSeconds before the plan the passenger has, on the
/// same times and boarding and leaving the trains as a search would (tripsArrival); a plan with a train that can no longer be
/// boarded in time does not reach it, and a plan whose times are not known is kept. Otherwise the passenger keeps waiting. A
/// passenger who waits for a train that is at most the late threshold late and not slowed is not planned again, because the plans
/// use the planned times of that train. The passengers whose train changed in a step plan in the order of the passenger list.
///
/// The transfers count over the whole journey: a plan from where the passenger is now counts the trains the journey has ridden, so
/// that the journey has at most passengerMaxTransfers + 1 trains. A passenger who has used them up, such as one set down by a train
/// that ended its service early, may still take one train. A passenger whose new plan boards the same train at the same platform as
/// before keeps the place in the line of that train (Trip::Actual_Departure_Time); a passenger who leaves the line counts the time
/// waited in it and goes to the back of the next one.
///
/// The searches of one phase of a step are shared: passengers who plan from one station at one time use one search. The
/// planning and the settling of the trains each start a phase, because the trains move between them.
class PassengerJourneys {
public:
	/// Seconds by which a new plan has to reach the destination before the plan of a passenger who waits for a late or slowed train
	/// for the passenger to take it, so that passengers do not switch back and forth on small changes of a prediction.
	static constexpr double kSwitchMarginSeconds = 60.0;

	/// Empties everything, including the router.
	void clear();

	/// The trains whose prediction (predictionSignature) changed since the last call and that are late or slowed now, in the order
	/// of the trains of the run: the passengers who wait for them, or ride them, have something new to plan on. Brings the states
	/// of the trains up to date for `t`. settleTrains calls it once a step, so a second caller in the same step gets nothing new.
	std::vector<int> trainsWithNewPrediction(const SimulationRun& run, int t);

	/// Starts a phase: the next plan sees the trains as they are now.
	void beginPhase();

	/// The journey of the passenger has just started at `t` (seconds from the start of the run): a journey without legs is
	/// planned, and a journey with legs is planned again when the train of its first leg is already gone. Changes the
	/// passenger only; the caller registers it with the flow.
	void startJourney(SimulationRun& run, Passenger& passenger, int t);

	/// After a passenger came to wait for a trip, such as after a transfer: plans again when the train of the trip cannot be
	/// boarded any more, and takes an earlier plan when the train is late or slowed (see the rule above). For a passenger who has just alighted, alightedAtPlatform is the number of the platform of the
	/// train that the passenger left (0 for a platform without a number), and the new plan starts there. The caller registers
	/// the passenger with the flow afterwards.
	void checkWaiting(SimulationRun& run, Passenger& passenger, int t, std::optional<int> alightedAtPlatform = std::nullopt);

	/// The train was full: the passenger plans again from this station without the train. The caller registers the passenger
	/// with the flow afterwards.
	void replanRefused(SimulationRun& run, Passenger& passenger, const Train& full, int t);


	/// The train has just arrived at its stop at `t`: the passengers who wait on its platform for another train board it when it
	/// reaches their destination without a change no later than their plan does (see the rule above).
	void offerStandingTrain(SimulationRun& run, PassengerFlow& flow, int train, int t);

	/// Settles the trains at the start of the passenger step at `t`: the passengers who wait for a train that has left a stop,
	/// or cannot call there any more, plan again, and the passengers on a train that ended its service are set down at its last
	/// stop and plan again from there. Then the passengers who wait for a train whose prediction changed look for an earlier
	/// plan (see the rule above).
	void settleTrains(SimulationRun& run, PassengerFlow& flow, int t);

private:
	struct Watch {
		int firstStop = 0; ///< advanceFirstStop of the train.
		int handled = 0;   ///< The stops before this one have had their waiting passengers planned again.
		bool ended = false;
	};
	void ensureBuilt(const SimulationRun& run);
	void ensureStates(const SimulationRun& run, int t);
	int stationIndex(const std::string& name) const;
	int trainIndex(const std::string& description) const;
	bool callsAt(const SimulationRun& run, int train, const std::string& station, const std::string& platformId) const;
	/// The earliest arrival at the destination of the journey from origin, for a passenger ready at readyAt (seconds of the day) on the
	/// platform with platformNumber, with the transfers that the journey has left after riding `ridden` trains (see the rule above).
	passenger_routing::Plan planFrom(const SimulationRun& run, const Journey& journey, const std::string& origin, int t, int readyAt, int platformNumber,
		std::size_t ridden);
	/// Plans the rest of the journey after keptTrips trips: with `given` that plan, otherwise the earliest arrival from the station.
	void planInto(SimulationRun& run, Passenger& passenger, Journey& journey, std::size_t keptTrips, int t, int readyAt, int platformNumber, bool isReplan,
		const passenger_routing::Plan* given = nullptr);
	void replanWaiting(SimulationRun& run, Passenger& passenger, int t, std::optional<int> alightedAtPlatform = std::nullopt,
		const passenger_routing::Plan* given = nullptr);
	/// The train of the leg that the passenger waits for left the stop without the passenger, or no longer calls at the platform:
	/// counts a missed connection when the passenger has ridden a train of the journey (Journey::MissedConnections), and plans
	/// again. The one place where a missed train is handled.
	void missedTrain(SimulationRun& run, Passenger& passenger, int t, std::optional<int> alightedAtPlatform = std::nullopt);
	/// The direct ride of the earliest train that stands at the platform of the passenger and beats the plan (see the rule above),
	/// or none; with onlyTrain at least 0, only that train. A passenger who alighted just now is ready after the walk from the
	/// platform of the train that was left.
	std::optional<passenger_routing::Plan> standingDirectPlan(const SimulationRun& run, const Passenger& passenger, const Journey& journey,
		const Trip& current, int t, std::optional<int> alightedAtPlatform, int onlyTrain = -1) const;
	void flushLeftStops(SimulationRun& run, PassengerFlow& flow, int train, int t);
	void setDownEndedTrain(SimulationRun& run, PassengerFlow& flow, int train, int t);
	void setDownEarly(SimulationRun& run, Passenger& passenger, const Train& train, int stop, int t);
	/// The passengers who wait for one of these trains at a stop that it still makes look for an earlier plan
	/// (switchToEarlierPlan), in the order of the passenger list.
	void offerEarlierPlans(SimulationRun& run, PassengerFlow& flow, const std::vector<int>& trains, int t);
	/// Plans again for a passenger who waits and takes the new plan when it reaches the destination more than kSwitchMarginSeconds
	/// before the plan the passenger has (see the rule above). True when it took the new plan; the caller then registers the
	/// passenger with the flow. alightedAtPlatform as for checkWaiting.
	bool switchToEarlierPlan(SimulationRun& run, Passenger& passenger, int t, std::optional<int> alightedAtPlatform);
	/// When the trips of the journey from index `from` on reach its destination on the times of states_, for a passenger who is
	/// ready at readyAt (seconds of the day) on the platform with platformNumber, and who boards the first of them at platformId.
	/// Each trip boards at the first call of its train at its station and platform, from TrainState::firstStop on, that the passenger
	/// reaches by TrainState::boardingDeadline (a call without times is reached), and leaves the train at the first later call at its
	/// arrival station that has an arrival, at TrainState::alightingTime: the rules of a search, which also cover a trip that boards
	/// where the timetable has no departure. passenger_routing::kUnreached when a train cannot be boarded in time or does not call
	/// where the trip says; below 0 when the trip leaves the train only where it has no arrival, so that the time is not known.
	double tripsArrival(const SimulationRun& run, const Journey& journey, std::size_t from, double readyAt, int platformNumber,
		const std::string& platformId) const;

	std::unique_ptr<passenger_routing::Router> router_;
	std::unordered_map<std::string, int> stationIndices_;
	std::unordered_map<std::string, int> trainIndices_;
	std::vector<Watch> watches_;
	std::vector<passenger_routing::TrainState> states_;
	/// What passengers have seen of the prediction of each train (predictionSignature) at the last call of trainsWithNewPrediction.
	std::vector<std::vector<int>> predictions_;
	bool statesFresh_ = false;
	int statesTime_ = 0;
	/// The searches of this phase by origin station, ready time, platform number and transfers.
	std::map<std::tuple<int, int, int, int>, passenger_routing::Search> searches_;
};

#endif // PASSENGERJOURNEYS_H
