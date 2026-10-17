#include "simulation/RollingStock.h"
#include "scene/SceneModel.h"
#include "scene/SignallingLevel.h"
#include "simulation/SimulationRun.h"
#include "util/Log.h"

#include <thread>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>

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
	NextBlockID = "None";
	ConnectedBlockingTimeID = "None";
	stationName = "None";
	trainDescription = "None";
	GeoPosStart = GeoPosEnd = -1;
	PosConnectedBlockingTime = -1;
	SpeedPreviousTrain = -1;
	SignallingLevel = kSignallingLevelUnset;
	LocationWithSwitch = false;
	SwitchName = "None";
	InfraElementInPositionForTrain = true;
	IsAlreadyUniformedToConnectedSwitch = false;
	IsEndOfDivSwitchBeingStartOfADivSwitch = false;
	NamePreviousTrain = "None";
	NextBlockIDPreviousTrain = "None";
}

// const int Max_N_Reg = 150;

Train::Train() {
	velocityIntervals = temp = stop = counter = counter2 = CounterFollowingMode = IsTrainCoupling = 0;
	BrakStep = -1;
	End_Time = -1; // setTrainVectorSizesFromInput sets the last step of the trajectory arrays
	numStations = 0;
	Xobmin = Vobmin = 0;
	// instant_train_energy_consumption[0] = 0; // moved to function that sets the size of vectors from user input (at this point vector size is zero, can't set value)
	brakingPoint = 0;
	delayed = 0;
	RunStartTime = 0;
	CanEnter = false;
	direction = 0;
	Final_Delay = TotalInputDelays = 0;
	N_Station_Stopped = 0;
	indexOfRoute = 0;
	OutOfSimulation = false;
	GradientExceptionInBraking = false;
	IsTrainStoppedForEoA = false;
	type = "None";
	ETCS3StoppingPoint = -1;
	Start_Node_X = -10000;
	EntranceDelay = 0;
	N_BlockSections = 0;
	N_BlockTimeComplete = 0;
	N_ConflictingTrains = 0;
	IsTrainInFollowingMode = IsTrainDecoupling = IsInUnintentionalDecoupling = false;
	LeadingTrainInFollowingMode = "None";
	CurrentServiceStopPlatform = "None";
	TotalEnergyConsumed = 0;
	TotalEnergySubstationRequest = 0;
	TotalEnergyConsWithRegBrak = 0;
	TotalEnergySubstRequestWithRegBrak = 0;
	EnergyForAuxiliaries = 0;
	numOverlaps = 0;
	MAX_OnBoard_Passengers = trainPassengerCapacity(number_of_wagons);
	Current_OnBoard_Passengers = 0; // It is considered that the train starts with no passengers onboard
	for (int i = 0; i < kMaxTimetableStations; i++) {
		StationArrivals[i] = -1;
		StationArrivalNames[i] = "None";
		StationDelay[i] = -1;
		StationConsecDelay[i] = -1;
		StationDisturbance[i] = 0;
	}

	prevIntendedDepTime = 0; // initialized as 0 to print from t=0 in the 1st service
	reservedPlatform = -1;

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

	// define parameters delta 1, delta 2 and delta 3
	double delta1 = 0, delta2 = 0, delta3 = 0;
	// delta1 measures the congestion at the platform if the platform occupancy rate is higher than 0.65 than delta1=1
	if (PlatformOccupancyRate > 0.65)
		delta1 = 1;

	// delta 3 measures the degree of congestion on board of the train. It becomes 1 if the onboard occupancy rate is larger than 0.7
	double OnboardOccupancyrate = passengerOccupancyRatio(this->Current_OnBoard_Passengers, this->MAX_OnBoard_Passengers);

	if (OnboardOccupancyrate > 0.7)
		delta3 = 1;

	// Split the boarding and alighting passengers equally among the coaches / wagons
	int N_cars = number_of_wagons + 1; // the one represents the power unit
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
	// We consider that train cars have 2 doors per car + 2 extra doors which pertain to the traction units
	int N_Doors_In_a_Car = 2;
	int Total_N_Doors_In_Train = N_Doors_In_a_Car * number_of_wagons + N_Doors_In_a_Car;

	// defining list of passengers boarding and alighting from each door in a car
	// they are defined by randomly drawin the number of passenger boarding or alighting from a door and deriving the remaining of that car from the randomly drawn number
	list<int> BoardPaxFromDoor;
	list<int> AlightPaxFromDoor;

	if (BoardingPassengersInACar.empty() != 1) {
		for (list<int>::iterator BoardInCar = BoardingPassengersInACar.begin(); BoardInCar != BoardingPassengersInACar.end(); BoardInCar++) {
			NumberGenerator& N = run_->numberGenerator;
			// The number of people boarding from the first door is drawn according to a Gaussian with mean equal to the total number of passenger boarding from that car/2 and standard deviation being 20% of the mean.
			int Nboarding_FirstDoorInACar = (int)N.getGaussianFloat((*BoardInCar / 2), (*BoardInCar / 2 * 0.20));
			int Nboarding_SecondDoorInACar = *BoardInCar - Nboarding_FirstDoorInACar;

			BoardPaxFromDoor.push_back(Nboarding_FirstDoorInACar);
			BoardPaxFromDoor.push_back(Nboarding_SecondDoorInACar);
		}
	}
	if (AlightingPassengersInACar.empty() != 1) {
		for (list<int>::iterator AlightFromCar = AlightingPassengersInACar.begin(); AlightFromCar != AlightingPassengersInACar.end(); AlightFromCar++) {
			NumberGenerator& N = run_->numberGenerator;
			// The number of people alighting from the first door is drawn according to a Gaussian with mean equal to the total number of passenger aligthing from that car/2 and standard deviation being 20% of the mean.
			int NAlight_FirstDoorInACar = (int)N.getGaussianFloat((*AlightFromCar / 2), (*AlightFromCar / 2 * 0.20));
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
		if (*B_Pax_Car > 15)
			delta2 = 1; // set delta2 to 1 if the number of passenger boarding from the car is larger than 15

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

Regional::Regional() {
	g = kSceneGravityMs2;
	ID = 0;
	mass_of_traction_unit = 0;
	mass_of_a_wagon = 0;
	number_of_wagons = 0;
	max_train_speed = 0;
	max_train_decelaration = 1.3;
	frontal_wagon_area = 1.45;
	resistanceCoefficient = 0;
	Jerk = 0;
	train_length = 0;
	massPerWagonAxle = mass_of_a_wagon * number_of_wagons;
	massFactor = sceneTrainMassFactor(SceneTrainPhysical());
	total_train_mass = mass_of_traction_unit + massPerWagonAxle;
}

// Function to compute Blocking Times of trains in mixed signalling areas
void ComputeBlockingTimesInMixedSignallingForAllTrains(SimulationRun& run, double SetupTime, double ReleaseTime, double SightReacTime, double SafetyMargin, string OutputFolder, double AbsRTSupplement, double PercRTSupplement) {
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

// Occupancy keeps its holder. Pending entries reserve the zone before their last braking opportunity, including
// same-direction followers. Temporary stops do not cancel a reservation; passage, termination or retargeting does.
void updateSingleTrackLocks(SimulationRun& run, int step) {
	if (run.infra.singleTrackLimits.empty() || timestep <= 0)
		return;
	if (run.infra.singleTrackHeld.size() != run.infra.singleTrackLimits.size())
		run.infra.singleTrackHeld.assign(run.infra.singleTrackLimits.size(), 0);
	if (run.infra.singleTrackReservations.size() != run.infra.singleTrackLimits.size())
		run.infra.singleTrackReservations.resize(run.infra.singleTrackLimits.size());
	const int index = step - static_cast<int>(run.signallingDelay / timestep);
	std::vector<int> forward(run.infra.singleTrackLimits.size(), 0), backward(run.infra.singleTrackLimits.size(), 0);
	std::vector<std::vector<SingleTrackReservation>> requests(run.infra.singleTrackLimits.size());
	std::vector<int> retainedDirection(run.infra.singleTrackLimits.size(), 0);
	for (int k = 0; k < run.numTrains && k < static_cast<int>(run.trains.size()); ++k) {
		Train& train = run.trains[k];
		if (train.OutOfSimulation || train.indexOfRoute < 0 || train.indexOfRoute >= static_cast<int>(run.infra.routes.size())
			|| !singleTrackRouteHasZone(run, train.indexOfRoute))
			continue;
		Route& route = run.infra.routes[train.indexOfRoute];
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
		for (std::size_t l = 0; l < run.infra.singleTrackLimits.size(); ++l) {
			const auto& intervals = singleTrackZone(run, l, train.indexOfRoute).intervals;
			for (const auto& interval : intervals) {
				if (!waiting && interval.first <= head && tail < interval.second) {
					(route.reversed_direction ? backward : forward)[l]++;
					break;
				}
			}
			for (const auto& interval : intervals) {
				if (tail >= interval.second)
					continue;
				const auto& pending = run.infra.singleTrackReservations[l];
				const bool retained = std::any_of(pending.begin(), pending.end(), [&](const SingleTrackReservation& owner) {
					return owner.trainIndex == k && owner.routeIndex == train.indexOfRoute && owner.trainDescription == train.trainDescription
						&& owner.departureTime == train.departure_time && owner.origin == train.Start_Node_X
						&& owner.destination == route.x_of_end_node && owner.interval == interval;
				});
				if (retained)
					retainedDirection[l] = route.reversed_direction ? -1 : 1;
				if (retained || ((!waiting || step + 1 >= train.departure_time) && train.needsSingleTrackReservation(head, speed, interval.first, route.sequence_of_block_sections.data(), route.N_Block_Sections)))
					requests[l].push_back({k, train.indexOfRoute, train.trainDescription, train.departure_time, train.Start_Node_X, route.x_of_end_node, interval});
			}
		}
	}
	for (std::size_t l = 0; l < run.infra.singleTrackLimits.size(); ++l) {
		int held = 0;
		if (run.infra.singleTrackHeld[l] < 0 && backward[l] > 0)
			held = -1;
		else if (forward[l] > 0)
			held = 1;
		else if (backward[l] > 0)
			held = -1;
		auto direction = [&run](const SingleTrackReservation& owner) { return run.infra.routes[owner.routeIndex].reversed_direction ? -1 : 1; };
		if (held == 0)
			held = retainedDirection[l];
		if (held == 0)
			for (const auto& request : requests[l])
				if (direction(request) > 0 || held == 0)
					held = direction(request); // forward wins only simultaneous requests on a free zone
		run.infra.singleTrackReservations[l].clear();
		for (const auto& request : requests[l])
			if (direction(request) == held)
				run.infra.singleTrackReservations[l].push_back(request);
		if (held != run.infra.singleTrackHeld[l]) {
			for (int r = 0; r < static_cast<int>(run.infra.routes.size()); ++r)
				for (const std::string& id : singleTrackZone(run, l, r).sectionIDs)
					if (std::find(run.blocksConnected.begin(), run.blocksConnected.end(), id) == run.blocksConnected.end())
						run.blocksConnected.push_back(id);
			run.infra.singleTrackHeld[l] = held;
		}
	}
}

// Function to Print out the blocking times of all the Trains
void PrintTimetablePoints(SimulationRun& run, string MainFolder) {
	string FileName;
	FileName = FileName + MainFolder + "/TimetablePoints.txt";
	ofstream OutputFile;
	OutputFile.open((char*)FileName.c_str(), ios::binary);

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
}

// Function to Print out the blocking times of all the Trains
void PrintTrainBlockingTimes(SimulationRun& run, string MainFolder) {
	string FileName;
	FileName = FileName + MainFolder + "/BlockingTimes.txt";
	ofstream OutputFile;
	OutputFile.open((char*)FileName.c_str(), ios::binary);

	for (int i = 0; i < run.numTrains; i++) {
		OutputFile << run.trains[i].trainDescription << "\n";

		for (int j = 0; j < run.trains[i].N_BlockTimeComplete; j++) {
			OutputFile << run.trains[i].BlockTime[j].BlockID << " ";
		}
		OutputFile << "\n";

		for (int j = 0; j < run.trains[i].N_BlockTimeComplete; j++) {
			/*if (train_route[regional_train[i].indexOfRoute].reversed_direction==0)
			OutputFile<<regional_train[i].BlockTime[j].PosStart<<" ";
			else
			OutputFile<<train_route[regional_train[i].indexOfRoute].OriginalRefReversedRoute-regional_train[i].BlockTime[j].PosEnd<<" ";*/
			OutputFile << run.trains[i].BlockTime[j].GeoPosStart << " ";
		}

		OutputFile << "\n";

		for (int j = 0; j < run.trains[i].N_BlockTimeComplete; j++) {
			/*if (train_route[regional_train[i].indexOfRoute].reversed_direction==0)
			OutputFile<<regional_train[i].BlockTime[j].PosEnd<<" ";
			else
			OutputFile<<train_route[regional_train[i].indexOfRoute].OriginalRefReversedRoute-regional_train[i].BlockTime[j].PosStart<<" ";*/
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
}

// Function to report the position of the trains in ETCS Level 3
void ReportAllTrainPositionsToRBC(SimulationRun& run, int i, double ETCS3SafetyMargin) {
	for (int j = 0; j < run.numTrains; j++) {
		run.trains[j].ReportPositionToRBC(i, run.infra.routes[run.trains[j].indexOfRoute].sequence_of_block_sections.data(), run.infra.routes[run.trains[j].indexOfRoute].N_Block_Sections, ETCS3SafetyMargin);
	}
}

// check train arrival/departure at/from destination/origin
void Train::checkTrainArrDep(int trainIdx, int t) {
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
	// for (int a = 0; a < numStations; a++) {
	//	if (std::fabs(X- Stations[a].X) < 0.001)
	//	{
	// cout << "\n<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<\n " <<
	//	trainDescription << " TRAIN at " << Stations[numStations - 1].stationName << " at t = " << t << endl;
	//	}
	//}
	// train stopped at last station
	if (((std::fabs(X - Stations[numStations - 1].X) < 0.001) || (std::fabs(std::fabs(X - Stations[numStations - 1].X) - train_length / 1000) < 0.001)) && (t >= 2) && (instant_train_speed[t - 2] != 0) && (instant_train_speed[t - 1] == 0) && (instant_train_speed[t] == 0)) { // position tolerance of 1m
		printTrainArrDepMsg(Stations[numStations - 1].stationName, "arr", trainIdx, t, initial_variables.OutputMainFolder + "/Rescheduling");
		EG_LOG(SimEvents, Info) << "\n<<< " << trainDescription << " ARRIVED at " << Stations[numStations - 1].stationName << " at t = " << t << " - " << simulationTime(t, initial_variables.startingSimulationTime) << "\n";
	}
	// train departing from origin
	else if (((std::fabs(X - Stations[0].X) < 0.001) || (std::fabs(std::fabs(X - Stations[0].X) - train_length / 1000) < 0.001)) && (t >= 1) && (instant_train_speed[t - 1] == 0) && (instant_train_speed[t] != 0)) { // position tolerance of 1m
		// printTrainArrDepMsg(Stations[0].stationName, "dep", trainIdx, t, "Input_EGTRAIN/Rescheduling");
		EG_LOG(SimEvents, Info) << "\n>>> " << trainDescription << " DEPARTED from " << Stations[0].stationName << " at t = " << t << " - " << simulationTime(t, initial_variables.startingSimulationTime) << "\n";
	}
}

// print train service path diagram (append to file)
void Train::printTrainServicePathDiagram(std::string FolderName, int nextServiceRouteID) {
	std::string FileName = FolderName + "/TrainServicePathDiagram.txt";
	std::ofstream FileOutput;

	// avoid different corridor for initial service (entering station for the first time)
	if (prevIntendedDepTime == 0 && nextServiceRouteID != -1 && run_->infra.routes[indexOfRoute].corridor != run_->infra.routes[nextServiceRouteID].corridor && run_->infra.routes[indexOfRoute].reversed_direction == run_->infra.routes[nextServiceRouteID].reversed_direction) {
		indexOfRoute = nextServiceRouteID;
	}

	FileOutput.open((char*)FileName.c_str(), std::ios::binary | std::ios::app); // append

	FileOutput << trainDescription << "\t" << dispLineID << "\t" << run_->infra.routes[indexOfRoute].reversed_direction << "\t" << run_->infra.routes[indexOfRoute].corridor << "\t";

	const int activeFirst = earliestActiveTrajectoryIndex < 0
		? -1
		: std::max(earliestActiveTrajectoryIndex, prevIntendedDepTime);
	const auto exportCells = trajectoryExportCells(instant_spatial_position, activeFirst, End_Time);
	for (int t = 0; t < initial_variables.times; t++) {
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

	FileOutput.close();
}

// print train arrival at terminal station (append to file)
void Train::printTrainArrDepMsg(std::string stationName, std::string msgType, int trainIdx, int t, std::string FolderName) {
	std::string FileName = FolderName + "/EGTRAINOutput.txt";
	std::ofstream FileOutput;

	FileOutput.open((char*)FileName.c_str(), std::ios::binary | std::ios::app); // append

	if (FileOutput.is_open()) {
		FileOutput << "egtrain," << stationName << "," << msgType << "," << dispLineID << "," << trainIdx << "," << t << "\n";

		FileOutput.close();

		// temporary solution to avoid file conflicts
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	} else // error opening file
	{
		EG_LOG(SimEvents, Error) << "Error2 opening file to write arr/dep message\n";
	}
}

// computes train X position (works with routes crossing different regions)
// used to get real X position (in routes with 2 regions, there are jumps when getting X from the position in the route directly)
// wagon = 0 by default to return head position
double Train::trainXPosition(int t, int wagon /*= 0*/) {
	double X;
	double portion = 0; // relative position from start_node (in meters)

	double total_nW = number_of_wagons + 1; // train.number_of_wagons does not include loco/first wagon

	// find occupied block section
	for (int i = 0; i < run_->infra.routes[indexOfRoute].N_Block_Sections; i++) {
		if (((instant_spatial_position[t] - wagon * (train_length / total_nW)) >= run_->infra.routes[indexOfRoute].sequence_of_block_sections[i].start_node.X * 1000) && (instant_spatial_position[t] - wagon * (train_length / total_nW)) < (run_->infra.routes[indexOfRoute].sequence_of_block_sections[i].end_node.X * 1000)) {
			portion = (instant_spatial_position[t] - wagon * (train_length / total_nW)) - run_->infra.routes[indexOfRoute].sequence_of_block_sections[i].start_node.X * 1000;

			// calculate X position
			if (run_->infra.routes[indexOfRoute].reversed_direction) {
				X = (run_->infra.routes[indexOfRoute].sequence_of_block_sections[i].GeoXBegNode - portion) / 1000;
			} else {
				X = (run_->infra.routes[indexOfRoute].sequence_of_block_sections[i].GeoXBegNode + portion) / 1000;
			}
			return X;
		}
	}

	// did not find position correctly
	return -1;
}

// function to protect all station areas
void protectStationAreas(SimulationRun& run, int i) {
	for (int k = 0; k < run.numTrains; k++) {
		// cout << ">>>>>>>>>" << regional_train[k].trainDescription << "is OutOfSimulation" << regional_train[k].OutOfSimulation << endl;
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

				const std::string stationName = run.trains[k].numStations > 0 && !run.trains[k].Stations.empty()
					? run.trains[k].Stations[run.trains[k].numStations - 1].stationName
					: std::string();
				auto platformBookedFor = [&](const StationBoundarySection& boundary) {
					// Legacy Netherlands dispatcher terminals with integer arrival-platform assignments.
					if (stationName != "Alm" && stationName != "Asd" && stationName != "Asdz")
						return false;
					if (boundary.exit)
						return false;
					for (int tr = 0; tr < run.numTrains; tr++) {
						if (run.trains[tr].ID == run.trains[k].ID && run.trains[tr].type == run.trains[k].type)
							continue;
						if (run.trains[tr].numStations <= 0 || run.trains[tr].Stations.empty())
							continue;
						if (run.trains[tr].Stations[run.trains[tr].numStations - 1].stationName == stationName
							&& run.trains[tr].reservedPlatform == run.trains[k].arrivalPlatform)
							return true;

						// wait for trains leaving the station to prevent deadlocks
						if (run.trains[tr].Stations[0].stationName != stationName
							|| run.trains[tr].indexOfRoute < 0
							|| run.trains[tr].indexOfRoute >= static_cast<int>(run.infra.routes.size())
							|| i >= static_cast<int>(run.trains[tr].instant_spatial_position.size()))
							continue;
						if (run.infra.routes[run.trains[tr].indexOfRoute].reversed_direction != route.reversed_direction) {
							// if the other train is not booking a platform, it is not at the platform - enough to check position
							if (!route.reversed_direction && run.trains[tr].numStations > 1
								&& run.trains[tr].trainXPosition(i) > run.trains[k].trainXPosition(i))
								return true;
							if (route.reversed_direction && run.trains[tr].numStations > 1
								&& run.trains[tr].trainXPosition(i) < run.trains[k].trainXPosition(i))
								return true;
						}
					}
					return false;
				};

				// occupy station interlocking area
				for (int s = 0; s < run.infra.stationBoundaries.size(); s++) {
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

						run.infra.stationBoundaries[s].protectEntrance(run, hHead + offset, run.trains[k].indexOfRoute,
							platformBookedFor(run.infra.stationBoundaries[s]));
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

						run.trains[k].reservedPlatform = run.trains[k].arrivalPlatform;
						break;
					}
				}
			}
		}
	}
}

// set vector sizes with length of simulation from user input
void Train::setTrainVectorSizesFromInput(int vec_size) {
	End_Time = vec_size - 1;
	// define vector sizes with length of simulation from user input
	instant_train_speed = std::vector<double>(vec_size, 0);
	instant_spatial_position = std::vector<double>(vec_size, 0);
	instant_train_power_consumption = std::vector<double>(vec_size, 0);
	instant_train_tractive_effort = std::vector<double>(vec_size, 0);
	instant_block_section_occupied = std::vector<std::string>(vec_size);
	instant_train_energy_consumption = std::vector<double>(vec_size, 0);
	BX = std::vector<double>(vec_size, 0);
	Xob = std::vector<double>(vec_size, 0);
	Vob = std::vector<double>(vec_size, 0);
	Eq = std::vector<int>(vec_size, 0);

	// previously on train class constructor (there won't work because size of vector is zero until user sets length of simulation and constructor is called before that)
	instant_train_energy_consumption[0] = 0;
}
