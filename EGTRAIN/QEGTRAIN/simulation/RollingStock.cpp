#include "simulation/RollingStock.h"
#include "scene/SceneModel.h"
#include "scene/SignallingLevel.h"
#include "simulation/RunOutputFile.h"
#include "simulation/SimulationRun.h"
#include "util/Log.h"
#include "util/timeutil.hpp"

#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <unordered_set>

using namespace std;

BlockingTimes::BlockingTimes() {
	StartOccTime = -1;
	EndOccTime = -1;
	length = 0;
	StartRunTime = EndRunTime = StartApproachTime = EndApproachTime = StartClearTime = EndClearTime = -1;
	setupTime = sightReacTime = ApproachTime = RunTime = clearingTime = ReleaseTime = -1;
	RunTimeMargin = -1;
	PosStart = -1;
	PosEnd = -1;
	IsComplete = true;
	NextBlockID = kNoneId;
	ConnectedBlockingTimeID = kNoneId;
	stationName = kNoneId;
	trainDescription = kNoneId;
	GeoPosStart = GeoPosEnd = -1;
	PosConnectedBlockingTime = -1;
	SpeedPreviousTrain = -1;
	SignallingLevel = kSignallingLevelUnset;
	LocationWithSwitch = false;
	SwitchName = kNoneId;
	InfraElementInPositionForTrain = true;
	IsAlreadyUniformedToConnectedSwitch = false;
	IsEndOfDivSwitchBeingStartOfADivSwitch = false;
	NamePreviousTrain = kNoneId;
	NextBlockIDPreviousTrain = kNoneId;
}

Train::Train() {
	velocityIntervals = temp = stop = counter = counter2 = CounterFollowingMode = IsTrainCoupling = 0;
	BrakStep = -1;
	End_Time = -1; // setTrainVectorSizesFromInput sets the last step of the trajectory arrays
	numStations = 0;
	Xobmin = Vobmin = 0;
	// instant_train_energy_consumption[0] = 0; // moved to function that sets the size of vectors from user input (at this point vector size is zero, can't set value)
	RunStartTime = 0;
	CanEnter = false;
	TotalInputDelays = 0;
	indexOfRoute = 0;
	OutOfSimulation = false;
	GradientExceptionInBraking = false;
	IsTrainStoppedForEoA = false;
	type = kNoneId;
	Start_Node_X = -10000;
	EntranceDelay = 0;
	N_BlockSections = 0;
	N_BlockTimeComplete = 0;
	IsTrainInFollowingMode = IsInUnintentionalDecoupling = false;
	LeadingTrainInFollowingMode = kNoneId;
	CurrentServiceStopPlatform = kNoneId;
	TotalEnergyConsumed = 0;
	TotalEnergySubstationRequest = 0;
	TotalEnergyConsWithRegBrak = 0;
	TotalEnergySubstRequestWithRegBrak = 0;
	EnergyForAuxiliaries = 0;
	MAX_OnBoard_Passengers = trainPassengerCapacity(number_of_wagons);
	Current_OnBoard_Passengers = 0; // It is considered that the train starts with no passengers onboard
	for (int i = 0; i < kMaxTimetableStations; i++) {
		StationArrivals[i] = -1;
		StationStandStarts[i] = -1;
		StationDepartures[i] = -1;
		StationArrivalNames[i] = kNoneId;
		StationDelay[i] = -1;
		StationConsecDelay[i] = -1;
		StationDisturbance[i] = 0;
	}

	for (int p = 0; p < 8; p++) {
		this->GibsonDwellTimeParameters[p] = -1;
	}
}

// By default it is considered that a carriage can transport maximum 300 passengers so it is 300 pax for the traction unit and 300 pax for each carriage / wagon
int trainPassengerCapacity(double numberOfWagons) {
	return static_cast<int>(300 + 300 * numberOfWagons);
}

double passengerOccupancyRatio(int passengers, int capacity) {
	if (capacity <= 0)
		return 0.0;
	return static_cast<double>(passengers) / capacity;
}

// Function to compute dwell times based on the interaction with passengers
// By default the dwell time is computed based on the microscopic dwell time model by Fernandez et al. (2007) which extends the model by Gibson et al. (1989)
// The dwell is 0 when nobody boards or alights, so the planned dwell of the stop applies.
double Train::computePaxDependentDwellTimeAtStations(int N_BoardPax, int N_AlightPax, double PlatformOccupancyRate, float beta0, float beta1, float beta2, float beta3, float beta4, float beta5, float beta6, float beta7) {
	if (N_BoardPax <= 0 && N_AlightPax <= 0)
		return 0.0;

	const InitialParameters& settings = run_->settings;

	// define parameters delta 1, delta 2 and delta 3
	double delta1 = 0, delta3 = 0;
	// delta1 measures the congestion at the platform if the platform occupancy rate is higher than the platform crowding threshold (0.65 by default) than delta1=1
	if (PlatformOccupancyRate > settings.passengerPlatformCrowdingThreshold)
		delta1 = 1;

	// delta 3 measures the degree of congestion on board of the train. It becomes 1 if the onboard occupancy rate is larger than the on-board crowding threshold (0.7 by default)
	double OnboardOccupancyrate = passengerOccupancyRatio(this->Current_OnBoard_Passengers, this->MAX_OnBoard_Passengers);

	if (OnboardOccupancyrate > settings.passengerOnboardCrowdingThreshold)
		delta3 = 1;

	// Split the boarding and alighting passengers equally among the coaches / wagons
	int N_cars = static_cast<int>(number_of_wagons) + 1; // the one represents the power unit
	list<int> BoardingPassengersInACar;
	list<int> AlightingPassengersInACar;

	if (N_cars > 1) {
		int CounterBoardPax = 0, CounterAlightingPax = 0;

		for (int i = 0; i < N_cars - 1; i++) {
			int averageBoardingPassengers = (int)(N_BoardPax / N_cars);
			BoardingPassengersInACar.push_back(averageBoardingPassengers);
			CounterBoardPax = CounterBoardPax + averageBoardingPassengers;

			int averageAlightingPassengers = (int)(N_AlightPax / N_cars);
			AlightingPassengersInACar.push_back(averageAlightingPassengers);
			CounterAlightingPax = CounterAlightingPax + averageAlightingPassengers;
		}
		// Now identifying the number of alighting and boarding passengers in the last car

		int BoardingPaxLastCar = N_BoardPax - CounterBoardPax;
		BoardingPassengersInACar.push_back(BoardingPaxLastCar); // Adding the board passengers of the last car to the list

		int AlightingPaxLastCar = N_AlightPax - CounterAlightingPax;
		AlightingPassengersInACar.push_back(AlightingPaxLastCar); // Adding the alighting passengers of the last car to the list

	}

	else { // if there is only one car then all passengers will aligth and board from the only car
		BoardingPassengersInACar.push_back(N_BoardPax);
		AlightingPassengersInACar.push_back(N_AlightPax);
	}

	// To apply the dwell time model by Gibson it is required to have the number of people boarding and alighting at each door
	// We consider that every car, the traction unit included, has 2 doors. The doors are counted from N_cars, the cars the
	// passengers were split over above, so a fractional number of wagons adds no door.
	int N_Doors_In_a_Car = 2;
	int Total_N_Doors_In_Train = N_Doors_In_a_Car * N_cars;

	// defining list of passengers boarding and alighting from each door in a car
	// they are defined by randomly drawin the number of passenger boarding or alighting from a door and deriving the remaining of that car from the randomly drawn number
	list<int> BoardPaxFromDoor;
	list<int> AlightPaxFromDoor;

	if (BoardingPassengersInACar.empty() != 1) {
		for (list<int>::iterator BoardInCar = BoardingPassengersInACar.begin(); BoardInCar != BoardingPassengersInACar.end(); BoardInCar++) {
			NumberGenerator& N = run_->numberGenerator;
			// The number of people boarding from the first door is drawn according to a Gaussian with mean equal to the total number of passenger boarding from that car/2 and standard deviation being 20% of the mean.
			int Nboarding_FirstDoorInACar = (int)N.getGaussianFloat((*BoardInCar / 2), (*BoardInCar / 2 * 0.20)); // NOLINT(bugprone-integer-division): the door split divides as ints
			int Nboarding_SecondDoorInACar = *BoardInCar - Nboarding_FirstDoorInACar;

			BoardPaxFromDoor.push_back(Nboarding_FirstDoorInACar);
			BoardPaxFromDoor.push_back(Nboarding_SecondDoorInACar);
		}
	}
	if (AlightingPassengersInACar.empty() != 1) {
		for (list<int>::iterator AlightFromCar = AlightingPassengersInACar.begin(); AlightFromCar != AlightingPassengersInACar.end(); AlightFromCar++) {
			NumberGenerator& N = run_->numberGenerator;
			// The number of people alighting from the first door is drawn according to a Gaussian with mean equal to the total number of passenger aligthing from that car/2 and standard deviation being 20% of the mean.
			int NAlight_FirstDoorInACar = (int)N.getGaussianFloat((*AlightFromCar / 2), (*AlightFromCar / 2 * 0.20)); // NOLINT(bugprone-integer-division): the door split divides as ints
			int NAlight_SecondDoorInACar = *AlightFromCar - NAlight_FirstDoorInACar;

			AlightPaxFromDoor.push_back(NAlight_FirstDoorInACar);
			AlightPaxFromDoor.push_back(NAlight_SecondDoorInACar);
		}
	}

	double Max_door_alight_board_time = -9999;
	list<double> Door_alight_board_time;

	int DoorInCarCounter = 0; // This counts the number of the car in the train. It is needed to check wheter there are more than 15 pboarding passengers on a car as this will set delta2 to 1

	list<int>::iterator B_Pax_Car = BoardingPassengersInACar.begin();
	list<int>::iterator B_Pax_Door = BoardPaxFromDoor.begin();
	list<int>::iterator A_Pax_Door = AlightPaxFromDoor.begin();

	for (int door = 0; door < Total_N_Doors_In_Train; door++) {
		double BoardProcessTime = 0, AlightProcessTime = 0;

		// Computing the board process time
		int delta2 = 0;
		if (*B_Pax_Car > settings.passengerCarBoardingThreshold)
			delta2 = 1; // set delta2 to 1 if the number of passenger boarding from the car is larger than the car boarding threshold (15 by default)

		// beta2 and beta5 are the times of one boarding and one alighting passenger. A door that nobody uses takes no time.
		if (*B_Pax_Door > 0)
			BoardProcessTime = (*B_Pax_Door) * beta2 + beta3 * delta1 + beta4 * delta2;
		if (*A_Pax_Door > 0)
			AlightProcessTime = (*A_Pax_Door) * (beta5 * exp(-beta6 * (*A_Pax_Door))) + beta7 * delta3;

		double Door_a_b_time = BoardProcessTime + AlightProcessTime;

		Door_alight_board_time.push_back(Door_a_b_time);
		B_Pax_Door++; // Advancing the iterator over the boarding doors of one element
		A_Pax_Door++; // Advancing the iterator over the alighting doors of one element

		DoorInCarCounter++;
		if (DoorInCarCounter == 2) { // if both doors of a car have been considered then advance the iterator on the boarding passengers per car (B_Pax_Car) to the next car
			B_Pax_Car++;
			DoorInCarCounter = 0; // and reset the car DoorinCarcounter to 0
		}
	}

	// Identifying the max door boarding alighting time
	if (Door_alight_board_time.empty() != 1) {
		for (list<double>::iterator door_time = Door_alight_board_time.begin(); door_time != Door_alight_board_time.end(); door_time++) {
			if (*door_time > Max_door_alight_board_time) {
				Max_door_alight_board_time = *door_time;
			}
		}
	}

	// finally computing the overall dwell time at the stop
	double Total_Dwell_Time = beta0 + beta1 * delta1 + Max_door_alight_board_time;

	// and returning it as output of the function
	return Total_Dwell_Time;
}

// Function to compute Blocking Times of trains in mixed signalling areas
void ComputeBlockingTimesInMixedSignallingForAllTrains(SimulationRun& run, double SetupTime, double ReleaseTime, double SightReacTime, double SafetyMargin, const string& OutputFolder, double AbsRTSupplement, double PercRTSupplement) {
	for (int i = 0; i < run.numTrains; i++) {

		run.trains[i].ComputeBlockingTimesInMixedSignallingAreas(SetupTime, ReleaseTime, SightReacTime, SafetyMargin, AbsRTSupplement, PercRTSupplement);
	}

	// Print the Files
	PrintTrainBlockingTimes(run, OutputFolder);
}

int Train::clampStationCount(int requested, const string& trainId) {
	if (requested <= kMaxTimetableStations)
		return requested;
	EG_LOG(SimSetup, Warning) << "Train " << trainId << " serves " << requested << " stations but the timetable arrays hold "
							  << kMaxTimetableStations << "; dropping the last " << requested - kMaxTimetableStations << " stops\n";
	return kMaxTimetableStations;
}
// Function to Determine for each Route the Block Sections that are occupied by trains
//(This Function Fill in the list BlocksOccupied)
void Occupy_Block_Sections_Of_Route(SimulationRun& run, int i) {
	for (int j = 0; j < run.numTrains; j++) {
		run.trains[j].Det_Section_Occupied_By_Train(i, run.infra.routes[run.trains[j].indexOfRoute].sequence_of_block_sections.data(), run.infra.routes[run.trains[j].indexOfRoute].N_Block_Sections);
	}
	updateSingleTrackLocks(run, i);
}

// New occupancy at an origin must not close a crossover after an approaching train has lost the
// opportunity to brake. Use the previous sample for every train, independent of movement order, and
// the existing next-step braking lookahead. No reservation survives a train safely stopping outside.
bool Train::entryConflictsWithCrossover(int step, const Section& origin) const {
	if (step <= 0 || timestep <= 0)
		return false;
	for (Train& approaching : run_->trains) {
		if (&approaching == this || !approaching.CanEnter || approaching.OutOfSimulation || step < approaching.departure_time
			|| approaching.indexOfRoute < 0 || approaching.indexOfRoute >= static_cast<int>(run_->infra.routes.size()))
			continue;
		Route& route = run_->infra.routes[approaching.indexOfRoute];
		for (int b = 0; b + 2 < route.N_Block_Sections; ++b) {
			const Section& first = route.sequence_of_block_sections[b];
			const Section& second = route.sequence_of_block_sections[b + 1];
			const Section& exit = route.sequence_of_block_sections[b + 2];
			if (!isFixedBlockLevel(first.SignallingLevel) || !first.end_node.virtualSignal || !second.start_node.virtualSignal)
				continue;
			if (origin.ID != exit.ID
				&& std::find(origin.IDConnectedBS, origin.IDConnectedBS + origin.N_ConnectedBS, exit.ID) == origin.IDConnectedBS + origin.N_ConnectedBS)
				continue;
			const int previous = step - 1;
			if (previous >= static_cast<int>(approaching.instant_spatial_position.size()) || previous >= static_cast<int>(approaching.instant_train_speed.size()))
				return true;
			const double head = approaching.instant_spatial_position[previous];
			const double speed = approaching.instant_train_speed[previous];
			if (!std::isfinite(head) || !std::isfinite(speed))
				return true;
			if (head - approaching.train_length >= exit.end_node.X * 1000)
				continue;
			if (approaching.needsSingleTrackReservation(head, speed, first.start_node.X * 1000,
					route.sequence_of_block_sections.data(), route.N_Block_Sections))
				return true;
		}
	}
	return false;
}

bool Train::entryCutsOffTrain(int step, const Section* BS) const {
	if (step <= 0)
		return false;
	const double head = Start_Node_X * 1000;
	const double tail = head - train_length;
	int under = entrySection; // the first section under this train
	while (under > 0 && BS[under - 1].end_node.X * 1000 > tail)
		--under;
	const bool movingBlock = isMovingBlockLevel(BS[entrySection].SignallingLevel);
	const bool reversed = run_->infra.routes[indexOfRoute].reversed_direction;
	for (Train& other : run_->trains) {
		// A train of the other direction is left to the codes and the single-track rules.
		if (&other == this || !other.CanEnter || other.OutOfSimulation || step < other.departure_time
			|| other.indexOfRoute < 0 || other.indexOfRoute >= static_cast<int>(run_->infra.routes.size())
			|| run_->infra.routes[other.indexOfRoute].reversed_direction != reversed)
			continue;
		Route& route = run_->infra.routes[other.indexOfRoute];
		for (int b = 0; b < route.N_Block_Sections; ++b) {
			const Section& met = route.sequence_of_block_sections[b];
			int k = under;
			while (k <= entrySection && BS[k].ID != met.ID)
				++k;
			if (k > entrySection)
				continue;
			const double behind = met.start_node.X * 1000 + tail - BS[k].start_node.X * 1000; // the tail on the route of other
			const int previous = step - 1;
			if (previous >= static_cast<int>(other.instant_spatial_position.size()) || previous >= static_cast<int>(other.instant_train_speed.size()))
				return true;
			const double position = other.instant_spatial_position[previous];
			const double speed = other.instant_train_speed[previous];
			if (!std::isfinite(position) || !std::isfinite(speed))
				return true;
			if (position > behind && position - other.train_length < behind + train_length)
				return true;
			// From standstill too: a train that leaves a stop in this step passes its node at once and may already have moved.
			const double limit = movingBlock ? behind : met.start_node.X * 1000;
			if (position < limit
				&& other.mayPassStopPoint(position, speed, limit, route.sequence_of_block_sections.data(), route.N_Block_Sections))
				return true;
			break;
		}
	}
	return false;
}

namespace {
// A train that the single-track rules look at in a step, at its position on its route in metres. Every waiting train is looked
// at, whatever its departure time; it is not in the network yet and stands at its entry with speed 0.
struct LockTrain {
	int index = -1;
	int route = -1;
	bool waiting = false;
	bool dwelling = false;
	double head = 0, tail = 0, speed = 0;
	/// By limit: the train is inside the zone or keeps a reservation of it.
	std::vector<char> holds;
};

// A train that stands on the way out of a zone for the target route, or cannot stop before it any more, or holds a further
// zone on it. start is the index on the route of the train of the first section of that way out.
struct WayOutBlock {
	std::size_t limit = 0;
	const LockTrain* train = nullptr;
	int target = -1;
	bool on = false;
	int start = -1;
};
} // namespace

// Occupancy keeps its holder. Pending entries reserve the zone before their last braking opportunity, including
// same-direction followers. A train at a service stop asks only when its dwell is over and its planned departure has come,
// so a reservation does not survive a service stop. Other temporary stops do not cancel a reservation; passage,
// termination or retargeting does.
// A new request is refused while a train of the other direction stands on the way out of the zone of the requesting route,
// or cannot stop before it any more. The requesting route then waits in front of the way out of that train's route, which
// stays closed as long as a train holds the zone or is on its way out of it.
void updateSingleTrackLocks(SimulationRun& run, int step) {
	RunInfrastructure& infra = run.infra;
	if (infra.singleTrackLimits.empty() || timestep <= 0)
		return;
	const std::size_t limits = infra.singleTrackLimits.size();
	const int routeCount = static_cast<int>(infra.routes.size());
	if (infra.singleTrackHeld.size() != limits)
		infra.singleTrackHeld.assign(limits, 0);
	if (infra.singleTrackReservations.size() != limits)
		infra.singleTrackReservations.resize(limits);
	const int index = step - static_cast<int>(run.signallingDelay / timestep);
	std::vector<int> forward(limits, 0), backward(limits, 0);
	std::vector<std::vector<SingleTrackReservation>> requests(limits);
	std::vector<std::vector<char>> retainedRequest(limits);
	std::vector<int> retainedDirection(limits, 0);
	std::vector<std::vector<int>> occupantRoutes(limits); // each route once
	std::vector<LockTrain> active;
	active.reserve(run.trains.size());
	std::vector<int> activeOf(run.trains.size(), -1); // by train index: its index in active, or -1
	for (int k = 0; k < run.numTrains && k < static_cast<int>(run.trains.size()); ++k) {
		Train& train = run.trains[k];
		if (train.OutOfSimulation || train.indexOfRoute < 0 || train.indexOfRoute >= routeCount
			|| !singleTrackRouteHasZone(run, train.indexOfRoute))
			continue;
		Route& route = infra.routes[train.indexOfRoute];
		const bool waiting = !train.CanEnter;
		if (!waiting && (step < train.departure_time || index < 0 || index >= static_cast<int>(train.instant_spatial_position.size())))
			continue;
		const double head = waiting ? train.Start_Node_X * 1000 : train.instant_spatial_position[index];
		const double tail = head - train.train_length;
		if (!std::isfinite(head))
			continue;
		const double speed = !waiting && index < static_cast<int>(train.instant_train_speed.size())
			? train.instant_train_speed[index]
			: 0.0;
		const bool dwelling = !waiting && train.dwellsAtServiceStop(step);
		active.push_back({k, train.indexOfRoute, waiting, dwelling, head, tail, speed, std::vector<char>(limits, 0)});
		std::vector<char>& holds = active.back().holds;
		activeOf[k] = static_cast<int>(active.size()) - 1;
		for (std::size_t l = 0; l < limits; ++l) {
			const auto& intervals = singleTrackZone(run, l, train.indexOfRoute).intervals;
			for (const auto& interval : intervals) {
				if (!waiting && interval.first <= head && tail < interval.second) {
					holds[l] = 1;
					(route.reversed_direction ? backward : forward)[l]++;
					std::vector<int>& occupants = occupantRoutes[l];
					if (std::find(occupants.begin(), occupants.end(), train.indexOfRoute) == occupants.end())
						occupants.push_back(train.indexOfRoute);
					break;
				}
			}
			for (const auto& interval : intervals) {
				if (tail >= interval.second || dwelling)
					continue;
				const auto& pending = infra.singleTrackReservations[l];
				const bool retained = std::any_of(pending.begin(), pending.end(), [&](const SingleTrackReservation& owner) {
					return owner.trainIndex == k && owner.routeIndex == train.indexOfRoute && owner.trainDescription == train.trainDescription
						&& owner.departureTime == train.departure_time && owner.origin == train.Start_Node_X
						&& owner.destination == route.x_of_end_node && owner.interval == interval;
				});
				if (retained) {
					retainedDirection[l] = route.reversed_direction ? -1 : 1;
					holds[l] = 1;
				}
				if (retained || ((!waiting || step + 1 >= train.departure_time) && train.needsSingleTrackReservation(head, speed, interval.first, route.sequence_of_block_sections.data(), route.N_Block_Sections))) {
					requests[l].push_back({k, train.indexOfRoute, train.trainDescription, train.departure_time, train.Start_Node_X, route.x_of_end_node, interval});
					retainedRequest[l].push_back(retained ? 1 : 0);
				}
			}
		}
	}
	std::vector<char> routeInUse(routeCount, 0);
	for (const LockTrain& train : active)
		routeInUse[train.route] = 1;
	auto sectionStart = [&infra](int route, int b) { return infra.routes[route].sequence_of_block_sections[b].start_node.X * 1000; };
	auto sectionEnd = [&infra](int route, int b) { return infra.routes[route].sequence_of_block_sections[b].end_node.X * 1000; };
	// Where a holder of zone l on route holder closes the way of each route of the other direction: in front of the way out
	// of the holder toward that route. SingleTrackZone::waitBefore names only routes of the other direction.
	auto closeForHolder = [&](std::size_t l, int holder, auto&& close) {
		for (int r = 0; r < routeCount; ++r) {
			const std::vector<int>& waitBefore = singleTrackZone(run, l, r).waitBefore;
			if (routeInUse[r] && holder < static_cast<int>(waitBefore.size()) && waitBefore[holder] >= 0)
				close(r, waitBefore[holder]);
		}
	};
	// A train past the zone holds its way out until its tail has left it.
	auto closeForLeavers = [&](std::size_t l, auto&& close) {
		for (const LockTrain& train : active) {
			const SingleTrackZone& zone = singleTrackZone(run, l, train.route);
			if (train.waiting || zone.last < 0 || train.head < sectionEnd(train.route, zone.last))
				continue;
			for (int r = 0; r < routeCount; ++r)
				if (routeInUse[r] && r < static_cast<int>(zone.exitUntil.size()) && zone.exitUntil[r] > zone.last
					&& train.tail < sectionEnd(train.route, zone.exitUntil[r]))
					close(r, singleTrackZone(run, l, r).waitBefore[train.route]);
		}
	};

	// Whether requester, which asks for zone l, has room at each stretch between two zones on its route up to the first one
	// where it waits on no way out of the other direction (or up to the end of its route): behind the trains of its direction
	// that are bound to wait there. A train is bound to wait at such a stretch when its route passes the stretch, it does not
	// hold the zone after it, and it holds l or is between the entry of l and the end of the stretch. given lists trains that
	// count as holding l, such as those that a new request gave l earlier in this step. The trains are queued from the entry of
	// the next zone, each in front of the section under the tail of the one ahead, as fixed blocks hold them. A stretch that no
	// train is bound for has room: a train alone may still wait inside a zone for the next one, which the ways out keep free of
	// the other direction. A requester already past the entry of l is not checked.
	auto roomAfter = [&](std::size_t l, const LockTrain& requester, const std::vector<const LockTrain*>& given) {
		const int r = requester.route;
		const Route& route = infra.routes[r];
		const int entry = singleTrackZone(run, l, r).first;
		if (requester.head > sectionStart(r, entry))
			return true;
		for (std::size_t from = l;;) {
			const SingleTrackZone& zone = singleTrackZone(run, from, r);
			if (zone.next < 0)
				return true;
			const std::size_t next = static_cast<std::size_t>(zone.next);
			const SingleTrackZone& after = singleTrackZone(run, next, r);
			std::unordered_set<std::string> stretch;
			for (int b = zone.last + 1; b < after.first; ++b)
				stretch.insert(route.sequence_of_block_sections[b].ID);
			auto bound = [&](const LockTrain& other, bool holdsZone) {
				const int end = singleTrackZone(run, next, other.route).first; // where the train waits on its own route
				if (other.index == requester.index || infra.routes[other.route].reversed_direction != route.reversed_direction
					|| end < 0 || other.holds[next] != 0)
					return false;
				const Route& otherRoute = infra.routes[other.route];
				bool passes = false;
				for (int b = 0; b < end && !passes; ++b)
					passes = stretch.count(otherRoute.sequence_of_block_sections[b].ID) != 0;
				const int start = std::max(singleTrackZone(run, l, other.route).first, 0);
				return passes
					&& (holdsZone
						|| (!other.waiting && other.head > sectionStart(other.route, start) && other.tail < sectionStart(other.route, end)));
			};
			double front = sectionStart(r, after.first);
			auto queue = [&](const LockTrain& train) {
				const double tail = front - run.trains[train.index].train_length;
				int b = after.first - 1;
				while (b > zone.last && sectionStart(r, b) > tail)
					--b;
				front = sectionStart(r, b);
				return b > zone.last;
			};
			bool waiting = false, room = true;
			for (const LockTrain& other : active)
				if (bound(other, other.holds[l] != 0)) {
					waiting = true;
					room = queue(other) && room;
				}
			for (const LockTrain* other : given)
				if (bound(*other, true)) {
					waiting = true;
					room = queue(*other) && room;
				}
			if (waiting && !(room && queue(requester)))
				return false;
			// The stretch is on a way out when a route of the other direction uses part of it after the next zone.
			bool onWayOut = false;
			for (int q = 0; q < routeCount && q < static_cast<int>(after.waitBefore.size()); ++q)
				onWayOut = onWayOut || (routeInUse[q] && after.waitBefore[q] >= 0 && after.waitBefore[q] < after.first);
			if (!onWayOut)
				return true;
			from = next;
		}
	};

	// Where a train waits when it finds no room after zone l: in front of the first way out of the zone for a route of the other
	// direction that it has not reached yet, where it blocks no train of that route, else at the zone.
	auto roomStop = [&](std::size_t l, const LockTrain& train) {
		const int r = train.route;
		const SingleTrackZone& zone = singleTrackZone(run, l, r);
		int stop = zone.first;
		for (int q = 0; q < routeCount && q < static_cast<int>(zone.waitBefore.size()); ++q)
			if (routeInUse[q] && zone.waitBefore[q] >= 0 && zone.waitBefore[q] < stop
				&& sectionStart(r, zone.waitBefore[q]) >= train.head)
				stop = zone.waitBefore[q];
		return stop;
	};

	// The new requests that find no room after their zone. They are refused whatever else this step decides, so the train stops in
	// front of the zone and does not count as heading for a way out behind it.
	std::vector<std::vector<char>> noRoom(limits);
	for (std::size_t l = 0; l < limits; ++l)
		for (std::size_t q = 0; q < requests[l].size(); ++q)
			noRoom[l].push_back(!retainedRequest[l][q]
						&& !roomAfter(l, active[static_cast<std::size_t>(activeOf[requests[l][q].trainIndex])], {})
					? 1
					: 0);

	// The trains on, or committed to, the way out of a zone for a route of the other direction.
	std::vector<WayOutBlock> blocks;
	for (std::size_t l = 0; l < limits; ++l)
		for (const LockTrain& train : active) {
			const SingleTrackZone& zone = singleTrackZone(run, l, train.route);
			if (zone.first < 0 || train.tail >= sectionStart(train.route, zone.first))
				continue;
			Route& route = infra.routes[train.route];
			for (int r = 0; r < routeCount && r < static_cast<int>(zone.waitBefore.size()); ++r) {
				const int start = zone.waitBefore[r];
				if (!routeInUse[r] || start < 0 || start >= zone.first)
					continue;
				const double from = sectionStart(train.route, start), to = sectionStart(train.route, zone.first);
				const bool on = !train.waiting && train.head > from;
				bool committed = !on && !train.waiting && !train.dwelling
					&& run.trains[train.index].needsSingleTrackReservation(train.head, train.speed, from, route.sequence_of_block_sections.data(), route.N_Block_Sections);
				for (std::size_t m = 0; m < limits && !on && !committed; ++m)
					for (std::size_t q = 0; q < requests[m].size(); ++q) {
						const SingleTrackReservation& request = requests[m][q];
						if (m != l && request.trainIndex == train.index && noRoom[m][q] == 0 && request.interval.first < to && request.interval.second > from)
							committed = true;
					}
				if (on || committed)
					blocks.push_back({l, &train, r, on, start});
			}
		}

	// The closures of this step, by limit * route count + route: in front of the way out of every holder and leaving train, and
	// of every blocking train. closedAt lists by route the places closed so far, in front of which a committed train stops.
	std::vector<int> closures(limits * routeCount, -1);
	std::vector<std::vector<int>> closedAt(routeCount);
	auto closeSlot = [&](std::size_t l, int r, int b) {
		int& closure = closures[l * routeCount + r];
		closure = closure < 0 ? b : std::min(closure, b);
	};
	// A holder or a leaving train closes a route only in front of the zone: a closure at the zone itself is the held direction.
	auto closeInFront = [&](std::size_t l, int r, int b) {
		if (b >= 0 && b < singleTrackZone(run, l, r).first)
			closeSlot(l, r, b);
	};
	for (std::size_t l = 0; l < limits; ++l) {
		auto closeAndList = [&](int r, int b) {
			closedAt[r].push_back(b);
			closeInFront(l, r, b);
		};
		for (int holder : occupantRoutes[l])
			closeForHolder(l, holder, closeAndList);
		// A retained request still has to win the zone below before it closes anything for the step.
		for (std::size_t q = 0; q < requests[l].size(); ++q)
			if (retainedRequest[l][q])
				closeForHolder(l, requests[l][q].routeIndex, [&closedAt](int r, int b) { closedAt[r].push_back(b); });
			else if (noRoom[l][q] != 0)
				closedAt[requests[l][q].routeIndex].push_back(
					roomStop(l, active[static_cast<std::size_t>(activeOf[requests[l][q].trainIndex])]));
		closeForLeavers(l, closeAndList);
	}
	// A train that waits to enter the network and has no room after its first zone stays out of the network, so that it does not
	// stand on the way out of that zone for a route of the other direction. The trains in the network that have still to reach
	// that zone, and the waiting trains let in before in this step, have the room first.
	auto firstZone = [&](const LockTrain& train) {
		int found = -1;
		for (std::size_t l = 0; l < limits; ++l) {
			const int first = singleTrackZone(run, l, train.route).first;
			if (first >= 0 && (found < 0 || first < singleTrackZone(run, static_cast<std::size_t>(found), train.route).first))
				found = static_cast<int>(l);
		}
		return found;
	};
	infra.singleTrackKeptOut.assign(run.trains.size(), 0);
	std::vector<const LockTrain*> letIn;
	for (const LockTrain& train : active) {
		if (!train.waiting || step + 1 < run.trains[train.index].departure_time)
			continue;
		const int l = firstZone(train);
		if (l < 0)
			continue;
		std::vector<const LockTrain*> ahead;
		for (const LockTrain& other : active)
			if (!other.waiting && firstZone(other) == l
				&& other.head <= sectionStart(other.route, singleTrackZone(run, static_cast<std::size_t>(l), other.route).first))
				ahead.push_back(&other);
		for (const LockTrain* other : letIn)
			if (firstZone(*other) == l)
				ahead.push_back(other);
		if (roomAfter(static_cast<std::size_t>(l), train, ahead)) {
			letIn.push_back(&train);
			continue;
		}
		infra.singleTrackKeptOut[static_cast<std::size_t>(train.index)] = 1;
	}
	// A train behind a closed section in front of its way out cannot reach it. A train on its way out blocks; of two trains that
	// are only committed, the forward one blocks; then the reversed trains that are still not held in front of their way out.
	std::vector<char> blocked(limits * routeCount, 0); // the new requests of the route for the zone are refused
	for (int phase = 0; phase < 3; ++phase)
		for (const WayOutBlock& block : blocks) {
			const bool forwardTrain = !infra.routes[block.train->route].reversed_direction;
			if ((phase == 0) != block.on || (phase == 1 && !forwardTrain) || (phase == 2 && forwardTrain))
				continue;
			const std::vector<int>& closed = closedAt[block.train->route];
			if (!block.on && std::any_of(closed.begin(), closed.end(), [&](int c) {
					return c >= 0 && c <= block.start && block.train->head < sectionStart(block.train->route, c);
				}))
				continue;
			const int at = singleTrackZone(run, block.limit, block.target).waitBefore[block.train->route];
			closedAt[block.target].push_back(at);
			closeSlot(block.limit, block.target, at);
			blocked[block.limit * routeCount + block.target] = 1;
		}

	for (std::size_t l = 0; l < limits; ++l) {
		int held = 0;
		if (infra.singleTrackHeld[l] < 0 && backward[l] > 0)
			held = -1; // NOLINT(bugprone-branch-clone): the branch order decides which direction wins
		else if (forward[l] > 0)
			held = 1;
		else if (backward[l] > 0)
			held = -1;
		auto direction = [&infra](const SingleTrackReservation& owner) { return infra.routes[owner.routeIndex].reversed_direction ? -1 : 1; };
		auto blockedRequest = [&](std::size_t q) { return !retainedRequest[l][q] && blocked[l * routeCount + requests[l][q].routeIndex] != 0; };
		std::vector<const LockTrain*> given; // the trains that a new request gave the zone in this step
		auto full = [&](std::size_t q) {
			return !retainedRequest[l][q] && !roomAfter(l, active[static_cast<std::size_t>(activeOf[requests[l][q].trainIndex])], given);
		};
		auto refused = [&](std::size_t q) { return blockedRequest(q) || full(q); };
		if (held == 0)
			held = retainedDirection[l];
		if (held == 0)
			for (std::size_t q = 0; q < requests[l].size(); ++q)
				if (!refused(q) && (direction(requests[l][q]) > 0 || held == 0))
					held = direction(requests[l][q]); // forward wins only simultaneous requests on a free zone
		infra.singleTrackReservations[l].clear();
		std::vector<std::pair<int, int>> waitOutside; // the route and stop of each request that found no room after the zone
		for (std::size_t q = 0; q < requests[l].size(); ++q) {
			if ((direction(requests[l][q]) != held && held != 0) || blockedRequest(q))
				continue;
			if (full(q)) {
				const int r = requests[l][q].routeIndex;
				waitOutside.emplace_back(r, roomStop(l, active[static_cast<std::size_t>(activeOf[requests[l][q].trainIndex])]));
				continue;
			}
			if (direction(requests[l][q]) != held)
				continue;
			infra.singleTrackReservations[l].push_back(requests[l][q]);
			closeForHolder(l, requests[l][q].routeIndex, [&](int r, int b) { closeInFront(l, r, b); });
			if (!retainedRequest[l][q])
				given.push_back(&active[static_cast<std::size_t>(activeOf[requests[l][q].trainIndex])]);
		}
		// A route without room is closed at the stop of the waiting train, unless a train of the route that holds the zone has still
		// to pass that stop: that train is ahead of the waiting one, which cannot pass it anyway.
		for (const auto& wait : waitOutside) {
			const int r = wait.first;
			const double stop = sectionStart(r, wait.second);
			const auto& owners = infra.singleTrackReservations[l];
			if (std::none_of(owners.begin(), owners.end(), [&](const SingleTrackReservation& owner) {
					const int a = activeOf[owner.trainIndex];
					return owner.routeIndex == r && a >= 0 && active[static_cast<std::size_t>(a)].head <= stop;
				}))
				closeSlot(l, r, wait.second);
		}
		if (held != infra.singleTrackHeld[l]) {
			for (int r = 0; r < routeCount; ++r)
				for (const std::string& id : singleTrackZone(run, l, r).sectionIDs)
					if (std::find(run.blocksConnected.begin(), run.blocksConnected.end(), id) == run.blocksConnected.end())
						run.blocksConnected.push_back(id);
			infra.singleTrackHeld[l] = held;
		}
	}

	// A changed closure releases every section that the closure marked for its route (occupySingleTrackForRoute): the first section
	// of the closed way out and the first section of the zone. Fixed-block levels rewrite the aspects of released sections only.
	std::vector<int>& previous = infra.singleTrackClosures;
	if (previous.size() != closures.size())
		previous.assign(closures.size(), -1);
	for (std::size_t slot = 0; slot < closures.size(); ++slot) {
		if (closures[slot] == previous[slot])
			continue;
		const int r = static_cast<int>(slot % routeCount);
		const Route& route = infra.routes[r];
		for (int b : {previous[slot], closures[slot], singleTrackZone(run, slot / routeCount, r).first})
			if (b >= 0 && b < route.N_Block_Sections) {
				const std::string& id = route.sequence_of_block_sections[b].ID;
				if (std::find(run.blocksConnected.begin(), run.blocksConnected.end(), id) == run.blocksConnected.end())
					run.blocksConnected.push_back(id);
			}
	}
	previous = std::move(closures);
}

// Function to Print out the blocking times of all the Trains
void PrintTimetablePoints(SimulationRun& run, const string& MainFolder) {
	string FileName;
	FileName = FileName + MainFolder + "/TimetablePoints.txt";
	ofstream OutputFile = openRunOutputFile(FileName, ios::binary);

	for (int i = 0; i < run.numTrains; i++) {
		OutputFile << run.trains[i].trainDescription << " " << run.trains[i].type << "\n";

		if (run.trains[i].TimetablePoints.empty() != 1) {
			for (list<TrainEvent>::iterator j = run.trains[i].TimetablePoints.begin(); j != run.trains[i].TimetablePoints.end(); j++) {
				OutputFile << j->SuccessorID << " ";
			}
		}
		OutputFile << "\n";

		if (run.trains[i].TimetablePoints.empty() != 1) {
			for (list<TrainEvent>::iterator j = run.trains[i].TimetablePoints.begin(); j != run.trains[i].TimetablePoints.end(); j++) {
				OutputFile << j->Position << " ";
			}
		}

		OutputFile << "\n";

		if (run.trains[i].TimetablePoints.empty() != 1) {
			for (list<TrainEvent>::iterator j = run.trains[i].TimetablePoints.begin(); j != run.trains[i].TimetablePoints.end(); j++) {
				OutputFile << j->Position << " ";
			}
		}

		OutputFile << "\n";

		if (run.trains[i].TimetablePoints.empty() != 1) {
			for (list<TrainEvent>::iterator j = run.trains[i].TimetablePoints.begin(); j != run.trains[i].TimetablePoints.end(); j++) {
				OutputFile << j->Time << " ";
			}
		}
		OutputFile << "\n";

		if (run.trains[i].TimetablePoints.empty() != 1) {
			for (list<TrainEvent>::iterator j = run.trains[i].TimetablePoints.begin(); j != run.trains[i].TimetablePoints.end(); j++) {
				OutputFile << j->Time2 << " ";
			}
		}
		OutputFile << "\n";
	}
	closeRunOutputFile(OutputFile, FileName);
}

// Function to Print out the blocking times of all the Trains
void PrintTrainBlockingTimes(SimulationRun& run, const string& MainFolder) {
	string FileName;
	FileName = FileName + MainFolder + "/BlockingTimes.txt";
	ofstream OutputFile = openRunOutputFile(FileName, ios::binary);

	for (int i = 0; i < run.numTrains; i++) {
		OutputFile << run.trains[i].trainDescription << "\n";

		for (int j = 0; j < run.trains[i].N_BlockTimeComplete; j++) {
			OutputFile << run.trains[i].BlockTime[j].BlockID << " ";
		}
		OutputFile << "\n";

		for (int j = 0; j < run.trains[i].N_BlockTimeComplete; j++) {
			OutputFile << run.trains[i].BlockTime[j].GeoPosStart << " ";
		}

		OutputFile << "\n";

		for (int j = 0; j < run.trains[i].N_BlockTimeComplete; j++) {
			OutputFile << run.trains[i].BlockTime[j].GeoPosEnd << " ";
		}

		OutputFile << "\n";

		for (int j = 0; j < run.trains[i].N_BlockTimeComplete; j++) {
			OutputFile << run.trains[i].BlockTime[j].StartOccTime << " ";
		}
		OutputFile << "\n";

		for (int j = 0; j < run.trains[i].N_BlockTimeComplete; j++) {
			OutputFile << run.trains[i].BlockTime[j].EndOccTime << " ";
		}
		OutputFile << "\n";
	}
	closeRunOutputFile(OutputFile, FileName);
}

// Function to report the position of the trains in ETCS Level 3
void ReportAllTrainPositionsToRBC(SimulationRun& run, int i, double ETCS3SafetyMargin) {
	for (int j = 0; j < run.numTrains; j++) {
		run.trains[j].ReportPositionToRBC(i, run.infra.routes[run.trains[j].indexOfRoute].sequence_of_block_sections.data(), run.infra.routes[run.trains[j].indexOfRoute].N_Block_Sections, ETCS3SafetyMargin);
	}
}

// check train arrival/departure at/from destination/origin
void Train::checkTrainArrDep(int t) {
	if (numStations <= 0 || Stations.empty())
		return;

	// add X to last station if needed (initialized as 0 in the beginning)
	if (Stations[numStations - 1].X == 0) {
		// find station position
		for (int j = 0; j < numStations; j++) {
			if (Stations[numStations - 1].stationName == run_->infra.stations[j].stationName) {
				Stations[numStations - 1].X = run_->infra.stations[j].X;
				break;
			}
		}
	}

	// add X to fisrt station if needed (initialized as 0 in the beginning)
	if (Stations[0].X == 0) {
		// find station position
		for (int j = 0; j < numStations; j++) {
			if (Stations[0].stationName == run_->infra.stations[j].stationName) {
				Stations[0].X = run_->infra.stations[j].X;
				break;
			}
		}
	}

	// train position
	double X = trainXPosition(t);

	// check arrivals and departures
	// train stopped at last station
	if (((std::fabs(X - Stations[numStations - 1].X) < 0.001) || (std::fabs(std::fabs(X - Stations[numStations - 1].X) - train_length / 1000) < 0.001)) && (t >= 2) && (instant_train_speed[t - 2] != 0) && (instant_train_speed[t - 1] == 0) && (instant_train_speed[t] == 0)) { // position tolerance of 1m
		EG_LOG(SimEvents, Info) << "\n<<< " << trainDescription << " ARRIVED at " << Stations[numStations - 1].stationName << " at t = " << t << " - " << simulationTime(t, run_->settings.startingSimulationTime) << "\n";
	}
	// train departing from origin
	else if (((std::fabs(X - Stations[0].X) < 0.001) || (std::fabs(std::fabs(X - Stations[0].X) - train_length / 1000) < 0.001)) && (t >= 1) && (instant_train_speed[t - 1] == 0) && (instant_train_speed[t] != 0)) { // position tolerance of 1m
		EG_LOG(SimEvents, Info) << "\n>>> " << trainDescription << " DEPARTED from " << Stations[0].stationName << " at t = " << t << " - " << simulationTime(t, run_->settings.startingSimulationTime) << "\n";
	}
}

// print train service path diagram (append to file)
void Train::printTrainServicePathDiagram(const std::string& FolderName, int nextServiceRouteID) {
	std::string FileName = FolderName + "/TrainServicePathDiagram.txt";

	// avoid different corridor for initial service (entering station for the first time)
	if (nextServiceRouteID != -1 && run_->infra.routes[indexOfRoute].corridor != run_->infra.routes[nextServiceRouteID].corridor && run_->infra.routes[indexOfRoute].reversed_direction == run_->infra.routes[nextServiceRouteID].reversed_direction) {
		indexOfRoute = nextServiceRouteID;
	}

	std::ofstream FileOutput = openRunOutputFile(FileName, std::ios::binary | std::ios::app); // append

	FileOutput << trainDescription << "\t" << -1 << "\t" << run_->infra.routes[indexOfRoute].reversed_direction << "\t" << run_->infra.routes[indexOfRoute].corridor << "\t";

	const int activeFirst = std::max(earliestActiveTrajectoryIndex, -1);
	const auto exportCells = trajectoryExportCells(instant_spatial_position, activeFirst, End_Time);
	for (int t = 0; t < run_->settings.times; t++) {
		const double position = t < static_cast<int>(exportCells.size()) ? exportCells[t] : -9999;
		if (position != -9999) {
			// non-reversed route (same with/without jump)
			if (!run_->infra.routes[indexOfRoute].reversed_direction) {
				FileOutput << position << "\t";
			}
			// reversed route without jump
			else if (run_->infra.routes[indexOfRoute].diffRegionsJumpX.first == 0) {
				FileOutput << (run_->infra.routes[indexOfRoute].OriginalRefReversedRoute - position) << "\t";
			}
			// reversed route with jump
			else {
				FileOutput << ((run_->infra.routes[indexOfRoute].OriginalRefReversedRoute - position) - (run_->infra.routes[indexOfRoute].diffRegionsJumpX.first * 1000)) << "\t";
			}
		} else {
			FileOutput << "\t";
		}
	}
	FileOutput << std::endl;

	closeRunOutputFile(FileOutput, FileName);
}

// computes train X position (works with routes crossing different regions)
// used to get real X position (in routes with 2 regions, there are jumps when getting X from the position in the route directly)
// wagon = 0 by default to return head position
double trainXPositionOf(const Train& train, int t, int wagon) {
	double X;
	double portion = 0; // relative position from start_node (in meters)

	double total_nW = train.number_of_wagons + 1; // train.number_of_wagons does not include loco/first wagon

	// find occupied block section
	const Route& route = train.run_->infra.routes[train.indexOfRoute];
	for (int i = 0; i < route.N_Block_Sections; i++) {
		if (((train.instant_spatial_position[t] - wagon * (train.train_length / total_nW)) >= route.sequence_of_block_sections[i].start_node.X * 1000) && (train.instant_spatial_position[t] - wagon * (train.train_length / total_nW)) < (route.sequence_of_block_sections[i].end_node.X * 1000)) {
			portion = (train.instant_spatial_position[t] - wagon * (train.train_length / total_nW)) - route.sequence_of_block_sections[i].start_node.X * 1000;

			// calculate X position
			if (route.reversed_direction) {
				X = (route.sequence_of_block_sections[i].GeoXBegNode - portion) / 1000;
			} else {
				X = (route.sequence_of_block_sections[i].GeoXBegNode + portion) / 1000;
			}
			return X;
		}
	}

	// did not find position correctly
	return -1;
}

double Train::trainXPosition(int t, int wagon /*= 0*/) {
	return trainXPositionOf(*this, t, wagon);
}

// function to protect all station areas
void protectStationAreas(SimulationRun& run, int i) {
	for (int k = 0; k < run.numTrains; k++) {
		if (!run.trains[k].OutOfSimulation) {
			if ((i >= run.trains[k].departure_time) && run.trains[k].CanEnter) {
				if (timestep <= 0 || i < 0 || i >= static_cast<int>(run.trains[k].instant_spatial_position.size())
					|| run.trains[k].indexOfRoute < 0 || run.trains[k].indexOfRoute >= static_cast<int>(run.infra.routes.size()))
					continue;
				const int delayedIndex = i - static_cast<int>(run.signallingDelay / timestep);
				if (delayedIndex < 0 || delayedIndex >= static_cast<int>(run.trains[k].instant_spatial_position.size()))
					continue;
				const Route& route = run.infra.routes[run.trains[k].indexOfRoute];
				if (route.N_Block_Sections <= 0)
					continue;

				// find signalling_block_sections occupied by head of train
				int hHead = -1;
				for (int h = 0; h < route.N_Block_Sections; h++) {
					if ((run.trains[k].instant_spatial_position[delayedIndex] < route.sequence_of_block_sections[h].end_node.X * 1000)
						&& (run.trains[k].instant_spatial_position[delayedIndex] >= route.sequence_of_block_sections[h].start_node.X * 1000)) {
						hHead = h;
						break;
					}
				}
				if (hHead < 0)
					continue;

				// occupy station interlocking area
				for (int s = 0; s < static_cast<int>(run.infra.stationBoundaries.size()); s++) {
					if (!run.infra.stationBoundaries[s].entrance)
						continue;
					bool stationAreaHandled = false;
					for (int offset : {1, 2}) {
						if (hHead + offset >= route.N_Block_Sections
							|| run.infra.stationBoundaries[s].entrance->ID != route.sequence_of_block_sections[hHead + offset].ID)
							continue;

						if (offset == 2) {
							// do not protect if section before entrance is occupied by another train, otherwise it will be blocked
							if (std::find(run.blocksOccupied.begin(), run.blocksOccupied.end(),
									route.sequence_of_block_sections[hHead + 1].ID)
								!= run.blocksOccupied.end()) {
								stationAreaHandled = true;
								break; // preserve the occupied-intermediate-section exception
							}
						}

						run.infra.stationBoundaries[s].protectEntrance(run, hHead + offset, run.trains[k].indexOfRoute);
						stationAreaHandled = true;
						break;
					}
					if (stationAreaHandled)
						break; // train occupies at most one station area

					// check if train entered station area
					if (run.infra.stationBoundaries[s].entrance->ID == route.sequence_of_block_sections[hHead].ID) {
						// FOR NOW, IGNORE AREAS WITH EXIT SECTION
						if (run.infra.stationBoundaries[s].exit) {
							continue;
						}
						break;
					}
				}
			}
		}
	}
}

// set vector sizes with length of simulation from user input
void Train::setTrainVectorSizesFromInput(int vec_size, bool detailedTrajectory) {
	End_Time = vec_size - 1;
	// define vector sizes with length of simulation from user input
	instant_train_speed = std::vector<double>(vec_size, 0);
	instant_spatial_position = std::vector<double>(vec_size, 0);
	instant_train_power_consumption = std::vector<double>(vec_size, 0);
	instant_train_tractive_effort = std::vector<double>(vec_size, 0);
	instant_block_section_occupied = std::vector<std::int32_t>(vec_size, kNoBlockSection);
	instant_train_energy_consumption = std::vector<double>(vec_size, 0);
	BX = detailedTrajectory ? std::vector<double>(vec_size, 0) : std::vector<double>();
	previousXob = previousVob = currentXob = currentVob = 0;
	stepBranch = 0;

	// previously on train class constructor (there won't work because size of vector is zero until user sets length of simulation and constructor is called before that)
	instant_train_energy_consumption[0] = 0;
}

void Train::recordOnBoardPassengers() {
	peakOnBoardPassengers = std::max(peakOnBoardPassengers, Current_OnBoard_Passengers);
	onBoardPassengerStepSum += Current_OnBoard_Passengers;
	++onBoardPassengerSteps;
}
