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

/// What the routing knows about the train at `elapsed` seconds from the start of the run, with firstStop from advanceFirstStop.
/// The delay is the arrival delay at the latest stop where the train arrived, and never negative. Before the train arrived anywhere
/// it is the lateness of its entry (the step of entering minus Train::departure_time) when it has entered, and 0 when it has not.
/// A train that still stands at its stop after the planned departure plus that delay is at least as late as the time it has stood
/// there, so that a passenger who comes to the platform now can still board it.
passenger_routing::TrainState liveTrainState(const Train& train, int firstStop, int elapsed);

/// The journeys of the passengers that the timetable of the run plans: journeys without legs, and journeys whose legs can no
/// longer be followed. It keeps the router of the run and what the planning needs of the trains, next to the PassengerFlow
/// state in SimulationRun, and is emptied with it.
///
/// A plan is the earliest arrival at the destination from the station where the passenger is now, with at most
/// SimulationRun::settings.passengerMaxTransfers transfers, on the planned times of the trains plus the latest delay each of
/// them is known to have (liveTrainState). Its rides become the trips of the journey, named <journey>.live<n>, with the
/// stations and platforms exactly as the stops of the train name them, so that boarding and alighting treat them like trips from
/// the scene. A passenger plans again from the station where it is when it misses its train (the train left, or cannot call
/// there any more), when the train is full, and when the train ends its service before the stop where the passenger leaves it.
/// A journey with no route ends with JourneyOutcome::NoRoute, and the passenger leaves the network at the station it is at.
///
/// A passenger who has a plan boards another train that stands at the platform where the passenger waits and reaches the
/// destination without a change no later than the plan does (with the delay the train has now, as the router counts it, and
/// when it has room); the plan becomes that train. It is looked at when the passenger comes to wait there (the start of a journey
/// with legs, or a transfer) for the trains that stand then, and when a train arrives for the passengers who wait there. The
/// train that the passenger has just left is not another train.
///
/// The searches of one phase of a step are shared: passengers who plan from one station at one time use one search. The
/// planning and the settling of the trains each start a phase, because the trains move between them.
class PassengerJourneys {
public:
	/// Empties everything, including the router.
	void clear();

	/// Starts a phase: the next plan sees the trains as they are now.
	void beginPhase();

	/// The journey of the passenger has just started at `t` (seconds from the start of the run): a journey without legs is
	/// planned, and a journey with legs is planned again when the train of its first leg is already gone. Changes the
	/// passenger only; the caller registers it with the flow.
	void startJourney(SimulationRun& run, Passenger& passenger, int t);

	/// After a passenger came to wait for a trip, such as after a transfer: plans again when the train of the trip cannot be
	/// boarded any more. For a passenger who has just alighted, alightedAtPlatform is the number of the platform of the
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
	/// stop and plan again from there.
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
	passenger_routing::Plan planFrom(const SimulationRun& run, const Journey& journey, const std::string& origin, int t, int readyAt, int platformNumber);
	/// Plans the rest of the journey after keptTrips trips: with `given` that plan, otherwise the earliest arrival from the station.
	void planInto(SimulationRun& run, Passenger& passenger, Journey& journey, std::size_t keptTrips, int t, int readyAt, int platformNumber, bool isReplan,
		const passenger_routing::Plan* given = nullptr);
	void replanWaiting(SimulationRun& run, Passenger& passenger, int t, std::optional<int> alightedAtPlatform = std::nullopt,
		const passenger_routing::Plan* given = nullptr);
	/// The direct ride of the earliest train that stands at the platform of the passenger and beats the plan (see the rule above),
	/// or none; with onlyTrain at least 0, only that train. A passenger who alighted just now is ready after the walk from the
	/// platform of the train that was left.
	std::optional<passenger_routing::Plan> standingDirectPlan(const SimulationRun& run, const Passenger& passenger, const Journey& journey,
		const Trip& current, int t, std::optional<int> alightedAtPlatform, int onlyTrain = -1) const;
	void flushLeftStops(SimulationRun& run, PassengerFlow& flow, int train, int t);
	void setDownEndedTrain(SimulationRun& run, PassengerFlow& flow, int train, int t);
	void setDownEarly(SimulationRun& run, Passenger& passenger, const Train& train, int stop, int t);

	std::unique_ptr<passenger_routing::Router> router_;
	std::unordered_map<std::string, int> stationIndices_;
	std::unordered_map<std::string, int> trainIndices_;
	std::vector<Watch> watches_;
	std::vector<passenger_routing::TrainState> states_;
	bool statesFresh_ = false;
	int statesTime_ = 0;
	std::map<std::tuple<int, int, int>, passenger_routing::Search> searches_;
};

#endif // PASSENGERJOURNEYS_H
