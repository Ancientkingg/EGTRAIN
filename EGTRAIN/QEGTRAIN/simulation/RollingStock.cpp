#include "simulation/RollingStock.h"
#include "scene/SceneModel.h"
#include "simulation/Optimisation.h"
#include "simulation/Passengers.h"

#include <thread>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <climits>
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <new>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

// GUI - Virtual Coupling notifications
vector<int> VCmsgTimestep;
vector<string> VCmsgTrain;
vector<string> VCmsgText;
// --------------

int N_OrderLists = initial_variables.num_OrderLists; // This is the number of OrderLists that have to be respected at critical nodes

OrderList::OrderList() {
	Node_X = -10000;
	numTeList = 0;
	LastEnteredTrain = "None";
	BlockID = "None";
	Is_DivergingJunction = Is_MergingJunction = 0;
}

OrderList OL[20];

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
	SignallingLevel = -99999999;
	LocationWithSwitch = false;
	SwitchName = "None";
	InfraElementInPositionForTrain = true;
	IsAlreadyUniformedToConnectedSwitch = false;
	IsEndOfDivSwitchBeingStartOfADivSwitch = false;
	NamePreviousTrain = "None";
	NextBlockIDPreviousTrain = "None";
}

int numRegions = 0; /*Number of Speed ranges in the characteristic Tractive effort-speed curve of trains, Number of Trains loaded for the simulation( N_Train+N_TrainD)*/

int N_Train, N_TrainD; /*Number of Trains with even path, Number of Trains with odd path*/

// const int Max_N_Reg = 150;

double VirtualQ[Max_N_Reg], VirtualQD[Max_N_Reg];

Train::Train() {
	velocityIntervals = temp = stop = counter = counter2 = CounterFollowingMode = BrakStep = IsTrainCoupling = 0;
	End_Time = (int)((initial_variables.times - 1) / timestep);
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
	XCurrentServiceStop = -1;
	TotalEnergyConsumed = 0;
	TotalEnergySubstationRequest = 0;
	TotalEnergyConsWithRegBrak = 0;
	TotalEnergySubstRequestWithRegBrak = 0;
	EnergyForAuxiliaries = 0;
	numOverlaps = 0;
	MAX_OnBoard_Passengers = 300 + 300 * number_of_wagons; // By default it is considered that a carriage can transport maximum 300 passengers so it is 300 pax for the traction unit and 300 pax for each carriage / wagon
	Current_OnBoard_Passengers = 0;						   // It is considered that the train starts with no passengers onboard
	for (int i = 0; i < kMaxTimetableStations; i++) {
		StationArrivals[i] = -1;
		StationArrivalNames[i] = "None";
		StationDelay[i] = -1;
		StationConsecDelay[i] = -1;
		StationDisturbance[i] = 0;
	}

	for (int i = 0; i < 100; i++) {
		for (int j = 0; j < 1000; j++) {
			HwMatrix[i][j] = -999999;
		}
	}

	prevIntendedDepTime = 0; // initialized as 0 to print from t=0 in the 1st service
	reservedPlatform = -1;

	for (int p = 0; p < 8; p++) {
		this->GibsonDwellTimeParameters[p] = -1;
	}
}

// Function to compute dwell times based on the interaction with passengers
// By default the dwell time is computed based on the microscopic dwell time model by Fernandez et al. (2007) which extends the model by Gibson et al. (1989)
double Train::computePaxDependentDwellTimeAtStations(int N_BoardPax, int N_AlightPax, double PlatformOccupancyRate, float beta0, float beta1, float beta2, float beta3, float beta4, float beta5, float beta6, float beta7) {
	// define parameters delta 1, delta 2 and delta 3
	double delta1 = 0, delta2 = 0, delta3 = 0;
	// delta1 measures the congestion at the platform if the platform occupancy rate is higher than 0.65 than delta1=1
	if (PlatformOccupancyRate > 0.65)
		delta1 = 1;

	// delta 3 measures the degree of congestion on board of the train. It becomes 1 if the onboard occupancy rate is larger than 0.7
	double OnboardOccupancyrate = this->Current_OnBoard_Passengers / this->MAX_OnBoard_Passengers;

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
			NumberGenerator N;
			// The number of people boarding from the first door is drawn according to a Gaussian with mean equal to the total number of passenger boarding from that car/2 and standard deviation being 20% of the mean.
			int Nboarding_FirstDoorInACar = (int)N.getGaussianFloat((*BoardInCar / 2), (*BoardInCar / 2 * 0.20));
			int Nboarding_SecondDoorInACar = *BoardInCar - Nboarding_FirstDoorInACar;

			BoardPaxFromDoor.push_back(Nboarding_FirstDoorInACar);
			BoardPaxFromDoor.push_back(Nboarding_SecondDoorInACar);
		}
	}
	if (AlightingPassengersInACar.empty() != 1) {
		for (list<int>::iterator AlightFromCar = AlightingPassengersInACar.begin(); AlightFromCar != AlightingPassengersInACar.end(); AlightFromCar++) {
			NumberGenerator N;
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
	list<int>::iterator A_Pax_Door = AlightPaxFromDoor.begin();

	for (int door = 0; door < Total_N_Doors_In_Train; door++) {
		double BoardProcessTime = 0, AlightProcessTime = 0;

		// Computing the board process time
		int delta2 = 0;
		if (*B_Pax_Car > 15)
			delta2 = 1; // set delta2 to 1 if the number of passenger boarding from the car is larger than 15

		BoardProcessTime = beta2 + beta3 * delta1 + beta4 * delta2;
		AlightProcessTime = beta5 * exp(-beta6 * (*A_Pax_Door)) + beta7 * delta3;

		double Door_a_b_time = BoardProcessTime + AlightProcessTime;

		Door_alight_board_time.push_back(Door_a_b_time);
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
	g = 9.81;
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
	massFactor = (1.09 * mass_of_traction_unit + 1.06 * massPerWagonAxle) / (mass_of_traction_unit + massPerWagonAxle);
	total_train_mass = mass_of_traction_unit + massPerWagonAxle;
}

Regional regional_train[Max_N_Reg];
Regional P, PD; /*This train is a Proof Train to measure the simulated Headway for each different Signalling Layout
				P is the Proof Train on the Even Track, while PD is the proof Train on the Odd track*/

namespace {

struct NativeStopPlan {
	std::string stationId;
	std::string stationName;
	std::string platformId;
	Node node;
	double dwellSeconds = 0.0;
	double plannedArrival = -1.0;
	double plannedDeparture = -1.0;
};

struct NativeTrainPlan {
	std::string routeId;
	std::string trainDescription;
	std::string type;
	SceneTrainPhysical physical;
	std::vector<std::array<double, 5>> tractionCurve;
	std::vector<NativeStopPlan> stops;
	int occurrence = 1;
	int routeIndex = -1;
	bool direction = false;
	double scheduledDeparture = 0.0;
	double entranceDelay = 0.0;
};

bool nativeFinite(double value) {
	return std::isfinite(value);
}

void addNativeDiagnostic(std::vector<SceneDiagnostic>& diagnostics, const std::string& code,
		const std::string& message, const std::string& file, const std::string& itemType,
		const std::string& itemId, const std::string& path = {}, const std::string& relatedId = {},
		const std::string& suggestedFix = {}, SceneSeverity severity = SceneSeverity::Error) {
	SceneDiagnostic diagnostic;
	diagnostic.severity = severity;
	diagnostic.code = code;
	diagnostic.message = message;
	diagnostic.file = file;
	diagnostic.itemType = itemType;
	diagnostic.itemId = itemId;
	diagnostic.path = path;
	diagnostic.relatedId = relatedId;
	diagnostic.suggestedFix = suggestedFix;
	diagnostics.push_back(std::move(diagnostic));
}

bool parseNativeBaseTime(const std::string& value, int& seconds) {
	if (value.size() != 8 || value[2] != ':' || value[5] != ':')
		return false;
	for (std::size_t i = 0; i < value.size(); ++i) {
		if (i == 2 || i == 5)
			continue;
		if (value[i] < '0' || value[i] > '9')
			return false;
	}
	const int hours = (value[0] - '0') * 10 + value[1] - '0';
	const int minutes = (value[3] - '0') * 10 + value[4] - '0';
	const int partSeconds = (value[6] - '0') * 10 + value[7] - '0';
	if (hours > 23 || minutes > 59 || partSeconds > 59)
		return false;
	seconds = hours * 3600 + minutes * 60 + partSeconds;
	return true;
}

template <typename T>
std::unordered_map<std::string, const T*> nativeIndexById(const std::vector<T>& values,
		std::vector<SceneDiagnostic>& diagnostics, const std::string& file,
		const std::string& itemType) {
	std::unordered_map<std::string, const T*> result;
	for (const T& value : values) {
		if (value.id.empty()) {
			addNativeDiagnostic(diagnostics, "scene.native.ref.id", "An item has an empty canonical ID",
					file, itemType, "");
			continue;
		}
		if (!result.emplace(value.id, &value).second)
			addNativeDiagnostic(diagnostics, "scene.native.ref.duplicate", "Duplicate canonical ID",
					file, itemType, value.id);
	}
	return result;
}

const ScenePlatform* nativePlatformForStation(const SceneStation& station, const std::string& platformId) {
	for (const ScenePlatform& platform : station.platforms)
		if (platform.id == platformId)
			return &platform;
	return nullptr;
}

const SceneStop* nativeStopForStation(const SceneService& service, const std::string& stationId) {
	for (const SceneStop& stop : service.stops)
		if (stop.stationId == stationId)
			return &stop;
	return nullptr;
}

const NativeStopPlan* nativeStopForStation(const NativeTrainPlan& train, const std::string& stationId) {
	for (const NativeStopPlan& stop : train.stops)
		if (stop.stationId == stationId)
			return &stop;
	return nullptr;
}

std::string nativeOccurrenceKey(const std::string& serviceId, int occurrence) {
	return serviceId + "\n" + std::to_string(occurrence);
}

int nativeRouteIndex(const std::string& routeId) {
	for (std::size_t index = 0; index < train_route.size(); ++index)
		if (train_route[index].ID == routeId)
			return static_cast<int>(index);
	return -1;
}

const SceneStation* nativeStationForId(
		const std::unordered_map<std::string, const SceneStation*>& stations,
		const std::string& stationId) {
	const auto found = stations.find(stationId);
	return found == stations.end() ? nullptr : found->second;
}

bool nativeRuntimeNodeMatchesStation(const Node& node, const SceneStation& station,
		const std::string& stationName) {
	return !node.stationPlatformId.empty() && node.stationPlatformId != "None"
			&& (node.stationName == stationName || node.stationName == station.id
					|| (node.station && node.stationName.empty()));
}

bool nativeRuntimePlatformExists(const std::string& platformId, const std::string& stationId,
		const std::string& stationName) {
	for (const StationPlatform& platform : AllStationPlatforms)
		if (platform.ID == platformId
				&& (platform.StationID == stationId || platform.StationID == stationName))
			return true;
	return false;
}

int nativeResolveRuntimeSection(const std::string& target,
		const std::unordered_set<std::string>& canonicalBlocks,
		const std::unordered_set<std::string>& canonicalSignals) {
	const bool isBlock = canonicalBlocks.count(target) != 0;
	const bool isSignal = canonicalSignals.count(target) != 0;
	if (!isBlock && !isSignal)
		return -1;
	const std::string signalToken = "@" + target + "@";
	for (int index = 0; index < Blocks; ++index) {
		const std::string runtimeId = signalling_block_sections[index].ID;
		if (isBlock && (runtimeId == target
				|| (runtimeId.size() > 2 && runtimeId.front() == '@' && runtimeId.back() == '@'
						&& runtimeId.substr(1, runtimeId.size() - 2) == target)))
			return index;
		if (isSignal && (runtimeId == target || runtimeId.find(signalToken) != std::string::npos))
			return index;
	}
	for (const Route& route : train_route) {
		for (const InfraElement& element : route.InfrastructureElements) {
			if (isSignal && element.ID == target) {
				for (int index = 0; index < Blocks; ++index)
					if (signalling_block_sections[index].ID == element.SectionID)
						return index;
			}
		}
	}
	return -1;
}

void nativeClearRegionalTrain(Regional& train) {
	delete[] train.Stations;
	train.Stations = nullptr;
}

void nativeCopyTrainPlan(const NativeTrainPlan& plan, Regional& train, int vectorSize) {
	train.g = 9.81;
	train.ID = plan.occurrence;
	train.type = plan.type;
	train.TrainRouteID = plan.routeId;
	train.indexOfRoute = plan.routeIndex;
	train.trainDescription = plan.trainDescription;
	train.direction = plan.direction;
	train.Start_Node_X = train_route[plan.routeIndex].x_of_start_node;
	train.mass_of_traction_unit = plan.physical.mass_of_traction_unit_kg;
	train.mass_of_a_wagon = plan.physical.mass_of_a_wagon_kg;
	train.number_of_wagons = plan.physical.number_of_wagons;
	train.max_train_speed = plan.physical.max_speed_ms;
	train.max_train_decelaration = plan.physical.max_deceleration_ms2;
	train.frontal_wagon_area = plan.physical.frontal_area_m2;
	train.resistanceCoefficient = plan.physical.resistance_coefficient;
	train.Jerk = plan.physical.jerk_ms3;
	train.train_length = plan.physical.length_m;
	train.massPerWagonAxle = train.mass_of_a_wagon * train.number_of_wagons;
	train.total_train_mass = train.mass_of_traction_unit + train.massPerWagonAxle;
	train.massFactor = (1.09 * train.mass_of_traction_unit + 1.06 * train.massPerWagonAxle)
			/ train.total_train_mass;
	train.velocityIntervals = static_cast<int>(plan.tractionCurve.size());
	for (int index = 0; index < train.velocityIntervals; ++index) {
		train.Vlb[index] = plan.tractionCurve[index][0];
		train.Vub[index] = plan.tractionCurve[index][1];
		train.C0[index] = plan.tractionCurve[index][2];
		train.C1[index] = plan.tractionCurve[index][3];
		train.C2[index] = plan.tractionCurve[index][4];
	}
	train.scheduled_departure_time = plan.scheduledDeparture;
	train.departure_time = plan.scheduledDeparture;
	train.EntranceDelay = plan.entranceDelay;
	train.TotalInputDelays = plan.entranceDelay;
	train.Initialise_Gibson_Dwell_Time_Parameters(7, 0.32, 18.23, 0.564, 4.838, 22.24, 0.04, 0.562);
	train.numStations = static_cast<int>(plan.stops.size());
	train.Stations = new Node[train.numStations];
	for (int index = 0; index < train.numStations; ++index) {
		train.Stations[index] = plan.stops[index].node;
		train.Stations[index].stationName = plan.stops[index].stationName;
		train.Stations[index].stationPlatformId = plan.stops[index].platformId;
		train.Stations[index].dwellTime = plan.stops[index].dwellSeconds;
		train.Stations[index].StopTime = plan.stops[index].dwellSeconds / timestep;
		train.StationArrivalNames[index] = plan.stops[index].stationName;
		train.ScheduledArrivals[index] = plan.stops[index].plannedArrival;
		train.ScheduledDepartures[index] = plan.stops[index].plannedDeparture;
	}
	train.setTrainVectorSizesFromInput(vectorSize);
	train.cacheStationPositions();
}

} // namespace

void resetNativeOperationsState() {
	N_OrderLists = 0;
	numRegions = 0;
	N_Train = 0;
	N_TrainD = 0;
	numAllStationPlatforms = 0;
	numAllDailyPassengers = 0;
	for (int index = 0; index < Max_N_Reg; ++index) {
		nativeClearRegionalTrain(regional_train[index]);
		regional_train[index].~Regional();
		new (&regional_train[index]) Regional();
	}
	for (OrderList& orderList : OL)
		orderList = OrderList();
	AllStationPlatforms.clear();
	AllDailyPassengers.clear();
	simulationIncidents.clear();
	VCmsgTimestep.clear();
	VCmsgTrain.clear();
	VCmsgText.clear();
}

std::vector<SceneDiagnostic> buildOperationsFromScene(const SceneModel& scene,
		const std::string& selectedScenarioId) {
	std::vector<SceneDiagnostic> diagnostics;
	nativeIndexById(scene.trainUnits, diagnostics, "trains.json", "train_unit");
	nativeIndexById(scene.compositions, diagnostics, "trains.json", "composition");
	const auto stationById = nativeIndexById(scene.stations, diagnostics, "stations.json", "station");
	const auto routes = nativeIndexById(scene.routes, diagnostics, "signalling.json", "route");
	const auto serviceById = nativeIndexById(scene.services, diagnostics, "services.json", "service");
	if (scene.services.empty())
		addNativeDiagnostic(diagnostics, "scene.native.services.none", "A runnable scene requires at least one service",
				"services.json", "service", "", "services");

	int baseTime = 0;
	if (!parseNativeBaseTime(scene.baseTime, baseTime))
		addNativeDiagnostic(diagnostics, "scene.native.time.base", "base_time must be HH:MM:SS",
				"scene.json", "scene", scene.name, "base_time");
	const double durationSeconds = initial_variables.durationOverride
			? initial_variables.times : scene.settings.durationSeconds;
	if (!scene.settings.hasDuration || !nativeFinite(durationSeconds)
			|| durationSeconds < 1.0
			|| durationSeconds > static_cast<double>(INT_MAX))
		addNativeDiagnostic(diagnostics, "scene.native.time.duration", "A positive finite simulation duration is required",
				"scene.json", "scene", scene.name, "settings.duration_seconds");
	if (scene.settings.hasBufferTime && (!nativeFinite(scene.settings.bufferTimeSeconds)
			|| scene.settings.bufferTimeSeconds < 0.0))
		addNativeDiagnostic(diagnostics, "scene.native.settings.buffer", "buffer_time_seconds must be finite and non-negative",
				"scene.json", "scene", scene.name, "settings.buffer_time_seconds");
	if (scene.settings.hasRecoveryTime && (!nativeFinite(scene.settings.recoveryTimePercent)
			|| scene.settings.recoveryTimePercent < 0.0))
		addNativeDiagnostic(diagnostics, "scene.native.settings.recovery", "recovery_time_percent must be finite and non-negative",
				"scene.json", "scene", scene.name, "settings.recovery_time_percent");

	const auto defaultScenario = [&]() -> const SceneScenario* {
		if (!selectedScenarioId.empty()) {
			for (const SceneScenario& scenario : scene.scenarios)
				if (scenario.id == selectedScenarioId)
					return &scenario;
			return nullptr;
		}
		if (!scene.defaultScenarioId.empty()) {
			for (const SceneScenario& scenario : scene.scenarios)
				if (scenario.id == scene.defaultScenarioId)
					return &scenario;
			return nullptr;
		}
		return scene.scenarios.empty() ? nullptr : &scene.scenarios.front();
	};
	const SceneScenario* scenario = defaultScenario();
	if (scenario == nullptr)
		addNativeDiagnostic(diagnostics, "scene.native.scenario", "The selected/default scenario does not exist",
				"scenarios.json", "scenario", selectedScenarioId.empty() ? scene.defaultScenarioId : selectedScenarioId);

	std::unordered_map<std::string, int> routeById;
	for (const auto& pair : routes) {
		const int runtimeIndex = nativeRouteIndex(pair.first);
		if (runtimeIndex < 0 || runtimeIndex >= static_cast<int>(train_route.size())
				|| train_route[runtimeIndex].N_Block_Sections <= 0)
			addNativeDiagnostic(diagnostics, "scene.native.ref.route", "Route is not available in the built runtime infrastructure",
					"signalling.json", "route", pair.first, "routes[" + pair.first + "]");
		else
			routeById[pair.first] = runtimeIndex;
	}

	std::unordered_map<std::string, int> repeatCounts;
	std::vector<NativeTrainPlan> trains;
	std::unordered_map<std::string, std::size_t> occurrenceIndex;
	for (const SceneService& service : scene.services) {
		if (service.id.empty())
			continue;
		if (service.stops.empty() && !service.through)
			addNativeDiagnostic(diagnostics, "scene.native.service.stops", "A service must contain at least one stop",
					"services.json", "service", service.id, "services[" + service.id + "].stops");
		if (service.stops.empty() && !service.through)
			continue;
		if (service.stops.size() > Train::kMaxTimetableStations)
			addNativeDiagnostic(diagnostics, "scene.native.capacity.stops", "Service stops exceed the runtime timetable capacity",
					"services.json", "service", service.id, "services[" + service.id + "].stops", {},
					std::to_string(Train::kMaxTimetableStations));
		const auto routeIt = routeById.find(service.route);
		if (routeIt == routeById.end())
			addNativeDiagnostic(diagnostics, "scene.native.ref.route", "Service route is unknown or unavailable",
					"services.json", "service", service.id, "services[" + service.id + "].route", service.route);
		SceneCompositionRuntime composition;
		std::string compositionDiagnostic;
		if (!buildSceneComposition(scene, service.composition, composition, compositionDiagnostic))
			addNativeDiagnostic(diagnostics, "scene.native.ref.composition",
					compositionDiagnostic.empty() ? "Service composition is unknown or invalid" : compositionDiagnostic,
					"trains.json", "service", service.id, "services[" + service.id + "].composition", service.composition);
		if (!composition.tractionCurve.empty()
				&& composition.tractionCurve.size() > 20)
			addNativeDiagnostic(diagnostics, "scene.native.capacity.traction", "Traction curve exceeds the runtime 20-band capacity",
					"trains.json", "composition", service.composition, "compositions[" + service.composition + "].units");
		if (!nativeFinite(composition.physical.mass_of_traction_unit_kg)
				|| !nativeFinite(composition.physical.mass_of_a_wagon_kg)
				|| !nativeFinite(composition.physical.number_of_wagons)
				|| !nativeFinite(composition.physical.max_speed_ms)
				|| !nativeFinite(composition.physical.max_deceleration_ms2)
				|| !nativeFinite(composition.physical.frontal_area_m2)
				|| !nativeFinite(composition.physical.resistance_coefficient)
				|| !nativeFinite(composition.physical.jerk_ms3)
				|| !nativeFinite(composition.physical.length_m)
			|| composition.physical.mass_of_traction_unit_kg < 0.0
			|| composition.physical.mass_of_a_wagon_kg < 0.0
			|| composition.physical.number_of_wagons < 0.0
			|| composition.physical.max_speed_ms <= 0.0
			|| composition.physical.max_deceleration_ms2 <= 0.0
			|| composition.physical.frontal_area_m2 < 0.0
			|| composition.physical.jerk_ms3 < 0.0
			|| composition.physical.length_m < 0.0
			|| composition.physical.mass_of_traction_unit_kg + composition.physical.mass_of_a_wagon_kg * composition.physical.number_of_wagons <= 0.0)
			addNativeDiagnostic(diagnostics, "scene.native.train.physical", "Composition physical values are non-finite or have no positive train mass",
					"trains.json", "service", service.id, "services[" + service.id + "].composition", service.composition);
		for (const auto& band : composition.tractionCurve) {
			if (!nativeFinite(band[0]) || !nativeFinite(band[1]) || !nativeFinite(band[2])
					|| !nativeFinite(band[3]) || !nativeFinite(band[4]) || band[1] <= band[0])
				addNativeDiagnostic(diagnostics, "scene.native.train.traction", "Composition contains an invalid traction band",
						"trains.json", "service", service.id, "services[" + service.id + "].composition", service.composition);
		}
		if (routeIt == routeById.end() || composition.tractionCurve.size() > 20)
			continue;

		int occurrences = 1;
		double headway = 0.0;
		if (service.hasRepeat) {
			headway = service.headwaySeconds;
			if (!nativeFinite(headway) || headway <= 0.0)
				addNativeDiagnostic(diagnostics, "scene.native.timetable.repeat", "Repeated services require a positive finite headway",
						"services.json", "service", service.id, "services[" + service.id + "].headway_seconds");
			else {
				const double rawCount = std::ceil(durationSeconds / headway);
				if (!nativeFinite(rawCount) || rawCount > static_cast<double>(INT_MAX))
					addNativeDiagnostic(diagnostics, "scene.native.capacity.occurrences", "Service repeat count exceeds the runtime integer capacity",
							"services.json", "service", service.id, "services[" + service.id + "].headway_seconds");
				else
					occurrences = std::max(1, static_cast<int>(rawCount));
			}
		}
		repeatCounts[service.id] = occurrences;
		if (occurrences > Max_N_Reg || trains.size() + static_cast<std::size_t>(occurrences) > Max_N_Reg) {
			addNativeDiagnostic(diagnostics, "scene.native.capacity.trains", "Expanded service occurrences exceed the runtime train capacity",
					"services.json", "service", service.id, "services[" + service.id + "].headway_seconds", {},
					std::to_string(Max_N_Reg));
			continue;
		}

		const Route& runtimeRoute = train_route[routeIt->second];
		if (service.hasEntryTime && (!nativeFinite(service.entryTimeSeconds) || service.entryTimeSeconds < 0.0))
			addNativeDiagnostic(diagnostics, "scene.native.timetable.entry", "Entry time must be finite and non-negative",
					"services.json", "service", service.id, "services[" + service.id + "].entry_seconds");
		std::vector<NativeStopPlan> baseStops;
		for (std::size_t stopIndex = 0; stopIndex < service.stops.size(); ++stopIndex) {
			const SceneStop& stop = service.stops[stopIndex];
			const SceneStation* station = nativeStationForId(stationById, stop.stationId);
			if (station == nullptr) {
				addNativeDiagnostic(diagnostics, "scene.native.ref.station", "Stop station is unknown",
						"services.json", "service", service.id, "services[" + service.id + "].stops[" + std::to_string(stopIndex) + "].station_id", stop.stationId);
				continue;
			}
			const std::string stationName = station->name.empty() ? station->id : station->name;
			std::string resolvedPlatformId = stop.platformId;
			const ScenePlatform* explicitPlatform = nullptr;
			if (!stop.platformId.empty()) {
				explicitPlatform = nativePlatformForStation(*station, stop.platformId);
				if (explicitPlatform == nullptr || !nativeRuntimePlatformExists(stop.platformId, station->id, stationName))
					addNativeDiagnostic(diagnostics, "scene.native.ref.platform", "Explicit stop platform is unknown or not built in the runtime infrastructure",
							"services.json", "service", service.id, "services[" + service.id + "].stops[" + std::to_string(stopIndex) + "].platform_id", stop.platformId);
			}
			std::unordered_set<std::string> candidatePlatforms;
			Node selectedNode;
			bool selected = false;
			const auto considerNode = [&](const Node& node) {
				if (!nativeRuntimeNodeMatchesStation(node, *station, stationName))
					return;
				if (explicitPlatform != nullptr && node.stationPlatformId != explicitPlatform->id)
					return;
				candidatePlatforms.insert(node.stationPlatformId);
				if (!selected) {
					selectedNode = node;
					selected = true;
				}
			};
			for (int sectionIndex = 0; sectionIndex < runtimeRoute.N_Block_Sections; ++sectionIndex) {
				const Section& section = runtimeRoute.sequence_of_block_sections[sectionIndex];
				considerNode(section.start_node);
				for (int arcIndex = 0; arcIndex < section.total_arcs; ++arcIndex)
					considerNode(section.arcs_in_signalling_block_section[arcIndex].endNode);
			}
			if (stop.platformId.empty()) {
				if (candidatePlatforms.size() > 1) {
					addNativeDiagnostic(diagnostics, "scene.native.ref.platform.ambiguous",
							"A stop without a platform resolves to multiple route platforms",
							"services.json", "service", service.id, "services[" + service.id + "].stops[" + std::to_string(stopIndex) + "].platform_id");
					continue;
				}
				if (!candidatePlatforms.empty())
					resolvedPlatformId = *candidatePlatforms.begin();
				else {
					// Legacy timetables may retain stops before a train enters, or after it
					// leaves, its simulated route. Keep those schedule rows without inventing
					// a platform assignment; they remain inert in route station matching.
					selectedNode.station = true;
					selectedNode.stationName = stationName;
					selectedNode.stationPlatformId = "None";
					if (station->hasPosition)
						selectedNode.X = station->positionKm;
					else if (!station->platforms.empty()) {
						for (const StationPlatform& platform : AllStationPlatforms) {
							if (platform.ID == station->platforms.front().id) {
								selectedNode.X = platform.X;
								selectedNode.Y = platform.Y;
								break;
							}
						}
					}
					selected = true;
				}
			}
			if (!selected) {
				if (!stop.platformId.empty())
					addNativeDiagnostic(diagnostics, "scene.native.ref.platform", "Explicit stop platform is not present on the service route",
							"services.json", "service", service.id, "services[" + service.id + "].stops[" + std::to_string(stopIndex) + "].platform_id", stop.platformId);
				continue;
			}
			if ((stop.hasPlannedArrival && !nativeFinite(stop.plannedArrivalSeconds))
					|| (stop.hasPlannedDeparture && !nativeFinite(stop.plannedDepartureSeconds))
					|| !nativeFinite(stop.dwellSeconds) || stop.dwellSeconds < 0.0)
				addNativeDiagnostic(diagnostics, "scene.native.timetable.stop", "Stop timetable values must be finite and dwell must be non-negative",
						"services.json", "service", service.id, "services[" + service.id + "].stops[" + std::to_string(stopIndex) + "]");
			if (stop.hasPlannedArrival && stop.hasPlannedDeparture
					&& stop.plannedDepartureSeconds < stop.plannedArrivalSeconds)
				addNativeDiagnostic(diagnostics, "scene.native.timetable.order", "Planned departure precedes planned arrival",
						"services.json", "service", service.id, "services[" + service.id + "].stops[" + std::to_string(stopIndex) + "]");
			NativeStopPlan stopPlan;
			stopPlan.stationId = stop.stationId;
			stopPlan.stationName = stationName;
			stopPlan.platformId = resolvedPlatformId;
			stopPlan.node = selectedNode;
			stopPlan.dwellSeconds = stop.dwellSeconds;
			stopPlan.plannedArrival = stop.hasPlannedArrival ? stop.plannedArrivalSeconds : -1.0;
			stopPlan.plannedDeparture = stop.hasPlannedDeparture ? stop.plannedDepartureSeconds : -1.0;
			baseStops.push_back(std::move(stopPlan));
		}

		for (int occurrence = 1; occurrence <= occurrences; ++occurrence) {
			NativeTrainPlan plan;
			plan.routeId = service.route;
			plan.trainDescription = service.id + "-" + std::to_string(occurrence);
			plan.type = service.id;
			plan.physical = composition.physical;
			plan.tractionCurve = composition.tractionCurve;
			plan.occurrence = occurrence;
			plan.routeIndex = routeIt->second;
			plan.direction = runtimeRoute.reversed_direction;
			const double offset = service.hasRepeat ? (occurrence - 1) * headway : 0.0;
			const double entry = service.hasEntryTime ? service.entryTimeSeconds
					: ((!service.stops.empty() && service.stops.front().hasPlannedDeparture
							&& nativeFinite(service.stops.front().plannedDepartureSeconds))
							? service.stops.front().plannedDepartureSeconds : 0.0);
			plan.scheduledDeparture = entry + offset;
			plan.stops = baseStops;
			for (NativeStopPlan& stop : plan.stops) {
				if (stop.plannedArrival >= 0.0)
					stop.plannedArrival += offset;
				if (stop.plannedDeparture >= 0.0)
					stop.plannedDeparture += offset;
			}
			occurrenceIndex.emplace(nativeOccurrenceKey(service.id, occurrence), trains.size());
			trains.push_back(std::move(plan));
		}
	}

	if (trains.size() > Max_N_Reg)
		addNativeDiagnostic(diagnostics, "scene.native.capacity.trains", "Expanded train count exceeds the runtime capacity",
				"services.json", "scene", scene.name, "services", {}, std::to_string(Max_N_Reg));

	std::vector<SimulationIncident> stagedIncidents;
	std::unordered_map<std::string, double> occurrenceDelay;
	std::unordered_set<std::string> appliedDelayStations;
	if (scenario != nullptr) {
		std::unordered_set<std::string> canonicalBlocks;
		std::unordered_set<std::string> canonicalSignals;
		for (const SceneBlock& block : scene.blocks)
			canonicalBlocks.insert(block.id);
		for (const SceneRoute& route : scene.routes)
			for (const std::string& block : route.blocks)
				canonicalBlocks.insert(block);
		for (const SceneSignal& signal : scene.signals)
			canonicalSignals.insert(signal.id);
		for (const SceneIncident& incident : scenario->incidents) {
			bool valid = true;
			if (incident.type != "signal_failure" && incident.type != "train_breakdown") {
				addNativeDiagnostic(diagnostics, "scene.native.incident.type", "Unknown incident type",
						"scenarios.json", "incident", incident.id, "incidents[" + incident.id + "].type");
				continue;
			}
			if (!nativeFinite(incident.startSeconds) || !nativeFinite(incident.endSeconds)
					|| incident.startSeconds < 0.0 || incident.endSeconds <= incident.startSeconds) {
				addNativeDiagnostic(diagnostics, "scene.native.incident.time", "Incident start/end must be finite with end after start",
						"scenarios.json", "incident", incident.id, "incidents[" + incident.id + "]");
				valid = false;
			}
			SimulationIncident runtimeIncident;
			runtimeIncident.type = incident.type;
			runtimeIncident.target = incident.target;
			runtimeIncident.startSeconds = incident.startSeconds;
			runtimeIncident.endSeconds = incident.endSeconds;
			if (incident.type == "signal_failure") {
				const int sectionIndex = nativeResolveRuntimeSection(incident.target, canonicalBlocks, canonicalSignals);
				if (sectionIndex >= 0)
					runtimeIncident.resolvedSectionIDs.push_back(signalling_block_sections[sectionIndex].ID);
				else {
					addNativeDiagnostic(diagnostics, "scene.native.incident.target", "Signal failure target does not resolve to an exact runtime section",
							"scenarios.json", "incident", incident.id, "incidents[" + incident.id + "].target", incident.target);
					valid = false;
				}
			} else if (serviceById.count(incident.target) == 0) {
				addNativeDiagnostic(diagnostics, "scene.native.incident.target", "Train breakdown target must be a canonical service ID",
						"scenarios.json", "incident", incident.id, "incidents[" + incident.id + "].target", incident.target);
				valid = false;
			}
			if (valid)
				stagedIncidents.push_back(std::move(runtimeIncident));
		}
		for (const SceneEntranceDelay& delay : scenario->entranceDelays) {
			const auto serviceIt = serviceById.find(delay.serviceId);
			const auto countIt = repeatCounts.find(delay.serviceId);
			const std::string key = nativeOccurrenceKey(delay.serviceId, delay.occurrence);
			if (serviceIt == serviceById.end() || countIt == repeatCounts.end() || delay.occurrence < 1
					|| delay.occurrence > countIt->second) {
				addNativeDiagnostic(diagnostics, "scene.native.entrance.ref", "Entrance delay service/occurrence is unknown",
						"scenarios.json", "entrance_delay", delay.serviceId, "entrance_delays", delay.serviceId);
				continue;
			}
			if (!nativeFinite(delay.delaySeconds) || delay.delaySeconds < 0.0) {
				addNativeDiagnostic(diagnostics, "scene.native.entrance.value", "Entrance delay must be finite and non-negative",
						"scenarios.json", "entrance_delay", delay.serviceId, "entrance_delays", delay.stationId);
				continue;
			}
			const SceneStop* stop = nativeStopForStation(*serviceIt->second, delay.stationId);
			if (stop == nullptr) {
				addNativeDiagnostic(diagnostics, "scene.native.entrance.station", "Entrance delay station is not a stop of the service",
						"scenarios.json", "entrance_delay", delay.serviceId, "entrance_delays", delay.stationId);
				continue;
			}
			if (!stop->hasPlannedDeparture) {
				addNativeDiagnostic(diagnostics, "scene.native.entrance.timetable", "Entrance delay cannot be applied to an absent planned departure",
						"scenarios.json", "entrance_delay", delay.serviceId, "entrance_delays", delay.stationId);
				continue;
			}
			const auto existing = occurrenceDelay.find(key);
			if (existing != occurrenceDelay.end() && existing->second != delay.delaySeconds) {
				addNativeDiagnostic(diagnostics, "scene.native.entrance.conflict", "Conflicting entrance delays target one service occurrence",
						"scenarios.json", "entrance_delay", delay.serviceId, "entrance_delays", delay.stationId);
				continue;
			}
			occurrenceDelay[key] = delay.delaySeconds;
			if (!appliedDelayStations.insert(key + "\n" + delay.stationId).second)
				continue;
			const auto trainIt = occurrenceIndex.find(key);
			if (trainIt == occurrenceIndex.end()) {
				addNativeDiagnostic(diagnostics, "scene.native.entrance.ref", "Entrance delay train occurrence was not built",
						"scenarios.json", "entrance_delay", delay.serviceId, "entrance_delays", delay.serviceId);
				continue;
			}
			NativeTrainPlan& train = trains[trainIt->second];
			train.entranceDelay = delay.delaySeconds;
			for (NativeStopPlan& trainStop : train.stops) {
				if (trainStop.stationId == delay.stationId) {
					trainStop.plannedDeparture += delay.delaySeconds;
					break;
				}
			}
		}
	}

	if (hasErrors(diagnostics))
		return diagnostics;

	InitialParameters stagedParameters = initial_variables;
	stagedParameters.name = scene.name;
	stagedParameters.startingSimulationTime = baseTime;
	stagedParameters.times = durationSeconds;
	stagedParameters.bufferTime = scene.settings.hasBufferTime
			? static_cast<int>(std::llround(scene.settings.bufferTimeSeconds)) : 0;
	stagedParameters.recoveryTimePercentage = scene.settings.hasRecoveryTime
			? static_cast<int>(std::llround(scene.settings.recoveryTimePercent)) : 0;
	stagedParameters.numTrackLines = static_cast<int>(scene.tracks.size());
	stagedParameters.N_Routes = static_cast<int>(scene.routes.size());
	stagedParameters.num_OrderLists = 0;

	const int vectorSize = std::max(1, static_cast<int>(std::ceil(durationSeconds / timestep)));
	std::list<StationPlatform> stagedPlatforms = AllStationPlatforms;
	for (StationPlatform& platform : stagedPlatforms) {
		const auto stationIt = stationById.find(platform.StationID);
		if (stationIt != stationById.end())
			platform.StationID = stationIt->second->name.empty() ? stationIt->second->id : stationIt->second->name;
		platform.length = 100;
		platform.width = 2.5;
		platform.Max_Passenger_Volume = static_cast<int>((platform.length * platform.width)
				/ (3.14159 * std::pow(0.8, 2)) * 0.8);
		platform.Current_N_Passengers = 0;
		platform.Current_List_Pax_On_Platform.clear();
		platform.List_Trains_Stopping_At_Platform.clear();
	}
	for (const NativeTrainPlan& train : trains) {
		for (const NativeStopPlan& stop : train.stops) {
			for (StationPlatform& platform : stagedPlatforms) {
				if (platform.ID == stop.platformId
						&& platform.StationID == stop.stationName) {
					platform.List_Trains_Stopping_At_Platform.push_back(train.trainDescription);
					break;
				}
			}
		}
	}

	std::list<Passenger> stagedPassengers;
	for (const ScenePassenger& sourcePassenger : scene.passengers) {
		Passenger passenger;
		passenger.ID = sourcePassenger.id;
		for (const ScenePassengerJourney& sourceJourney : sourcePassenger.journeys) {
			Journey journey;
			journey.ID = sourceJourney.id;
			journey.Journey_Activity_Type = sourceJourney.activity;
			const SceneStation* origin = nativeStationForId(stationById, sourceJourney.originStationId);
			const SceneStation* destination = nativeStationForId(stationById, sourceJourney.destinationStationId);
			if (origin == nullptr || destination == nullptr) {
				addNativeDiagnostic(diagnostics, "scene.native.passenger.station", "Passenger journey station is unknown",
					"passengers.json", "journey", sourceJourney.id, "passengers[" + sourcePassenger.id + "].journeys",
						origin == nullptr ? sourceJourney.originStationId : sourceJourney.destinationStationId);
				continue;
			}
			if (!nativeFinite(sourceJourney.plannedDepartureStartSeconds)
					|| !nativeFinite(sourceJourney.plannedDepartureEndSeconds)
					|| !nativeFinite(sourceJourney.plannedArrivalStartSeconds)
					|| !nativeFinite(sourceJourney.plannedArrivalEndSeconds)
					|| sourceJourney.plannedDepartureStartSeconds < 0.0
					|| sourceJourney.plannedDepartureEndSeconds < sourceJourney.plannedDepartureStartSeconds
					|| sourceJourney.plannedArrivalStartSeconds < 0.0
					|| sourceJourney.plannedArrivalEndSeconds < sourceJourney.plannedArrivalStartSeconds) {
				addNativeDiagnostic(diagnostics, "scene.native.passenger.window", "Passenger journey windows are invalid",
						"passengers.json", "journey", sourceJourney.id, "passengers[" + sourcePassenger.id + "].journeys");
				continue;
			}
			journey.Dep_Station_ID = origin->name.empty() ? origin->id : origin->name;
			journey.Arr_Station_ID = destination->name.empty() ? destination->id : destination->name;
			journey.Planned_Departure_Time = sourceJourney.plannedDepartureStartSeconds;
			journey.Planned_Arrival_Time = sourceJourney.plannedArrivalStartSeconds;
			for (const ScenePassengerLeg& sourceLeg : sourceJourney.legs) {
				const auto trainIt = occurrenceIndex.find(nativeOccurrenceKey(sourceLeg.serviceId, sourceLeg.occurrence));
				if (trainIt == occurrenceIndex.end() && serviceById.count(sourceLeg.serviceId) != 0) {
					addNativeDiagnostic(diagnostics, "scene.native.passenger.occurrence",
							"Passenger leg refers to a service occurrence outside the simulation horizon",
							"passengers.json", "leg", sourceLeg.id,
							"passengers[" + sourcePassenger.id + "].journeys[" + sourceJourney.id + "].legs",
							sourceLeg.serviceId + "-" + std::to_string(sourceLeg.occurrence), {}, SceneSeverity::Warning);
					continue;
				}
				const auto originStop = trainIt == occurrenceIndex.end() ? nullptr
						: nativeStopForStation(trains[trainIt->second], sourceLeg.originStationId);
				const auto destinationStop = trainIt == occurrenceIndex.end() ? nullptr
						: nativeStopForStation(trains[trainIt->second], sourceLeg.destinationStationId);
				if (trainIt == occurrenceIndex.end() || originStop == nullptr || destinationStop == nullptr) {
					addNativeDiagnostic(diagnostics, "scene.native.passenger.leg", "Passenger leg does not resolve to one train occurrence and two stops",
							"passengers.json", "leg", sourceLeg.id, "passengers[" + sourcePassenger.id + "].journeys[" + sourceJourney.id + "].legs", sourceLeg.serviceId);
					continue;
				}
				Trip trip;
				trip.TripID = sourceLeg.id;
				trip.JourneyID = journey.ID;
				trip.Trip_Activity_Type = journey.Journey_Activity_Type;
				trip.Dep_Station_ID = originStop->stationName;
				trip.Arr_Station_ID = destinationStop->stationName;
				trip.Dep_Station_Platform_ID = originStop->platformId;
				trip.Arr_Station_Platform_ID = destinationStop->platformId;
				trip.TrainServiceDescription = trains[trainIt->second].trainDescription;
				journey.Trips.push_back(std::move(trip));
			}
			journey.N_Trips = static_cast<int>(journey.Trips.size());
			passenger.Journeys.push_back(std::move(journey));
		}
		stagedPassengers.push_back(std::move(passenger));
	}
	if (hasErrors(diagnostics))
		return diagnostics;

	const auto sampleWindow = [](double minimum, double maximum) {
		return minimum + static_cast<double>(std::rand()) / RAND_MAX * (maximum - minimum);
	};
	auto stagedPassenger = stagedPassengers.begin();
	for (const ScenePassenger& sourcePassenger : scene.passengers) {
		auto stagedJourney = stagedPassenger->Journeys.begin();
		for (const ScenePassengerJourney& sourceJourney : sourcePassenger.journeys) {
			stagedJourney->Actual_Planned_Departure_Time = sampleWindow(
					sourceJourney.plannedDepartureStartSeconds, sourceJourney.plannedDepartureEndSeconds);
			stagedJourney->Actual_Planned_Arrival_Time = sampleWindow(
					sourceJourney.plannedArrivalStartSeconds, sourceJourney.plannedArrivalEndSeconds);
			if (!stagedJourney->Trips.empty()) {
				stagedJourney->Trips.front().Planned_Departure_Time =
						static_cast<int>(stagedJourney->Actual_Planned_Departure_Time);
				stagedJourney->Trips.back().Planned_Arrival_Time =
						static_cast<int>(stagedJourney->Actual_Planned_Arrival_Time);
			}
			++stagedJourney;
		}
		++stagedPassenger;
	}

	initial_variables = stagedParameters;
	if (!initial_variables.bufferTimeOverride)
		bufferTime = initial_variables.bufferTime;
	if (!initial_variables.recoveryTimeOverride)
		recoveryTimePercentage = initial_variables.recoveryTimePercentage;
	N_OrderLists = 0;
	numRegions = static_cast<int>(trains.size());
	N_Train = 0;
	N_TrainD = 0;
	for (const NativeTrainPlan& train : trains)
		if (train.direction)
			++N_TrainD;
		else
			++N_Train;
	for (int index = 0; index < Max_N_Reg; ++index) {
		nativeClearRegionalTrain(regional_train[index]);
		regional_train[index].~Regional();
		new (&regional_train[index]) Regional();
	}
	for (std::size_t index = 0; index < trains.size(); ++index)
		nativeCopyTrainPlan(trains[index], regional_train[index], vectorSize);
	changeTrainDepartureTimesForHourlyTimetabling(regional_train, numRegions);
	AllStationPlatforms = std::move(stagedPlatforms);
	numAllStationPlatforms = static_cast<int>(AllStationPlatforms.size());
	AllDailyPassengers = std::move(stagedPassengers);
	numAllDailyPassengers = static_cast<int>(AllDailyPassengers.size());
	simulationIncidents = std::move(stagedIncidents);
	return diagnostics;
}

void compareImplementedOrderWithRomaSolutionForAllOl(string FolderName, int t) {
	for (int i = 1; i < N_OrderLists; i++) {
		OL[i].compareImplementedOrderWithRomaSolution(FolderName, i, t);
	}
}

// Function to compute the Blocking Times in ETCS level 3 for all trains
void ComputeBlockingTimesETCS3ForAllTrains(double SetupTime, double ReleaseTime, double SightReacTime, string OutputFolder, double AbsRTSupplement, double PercentRTSupplement) {
	for (int i = 0; i < numRegions; i++) {

		// regional_train[i].ComputeBlockTimeETCS3(SetupTime,ReleaseTime,SightReacTime,AbsRTSupplement,PercentRTSupplement);
		regional_train[i].ComputeBlockTimeETCS3(SetupTime, ReleaseTime, SightReacTime, AbsRTSupplement, PercentRTSupplement);
	}

	// Print the Files
	PrintTrainBlockingTimes(OutputFolder);
}

// Function to compute blocking times of trains in ETCS L2 or conventional signalling
void ComputeBlockingTimesForAllTrains(string SignallingType, double SetupTime, double ReleaseTime, double SightReacTime, string OutputFolder, double AbsRTSupplement, double PercRTSupplement) {
	for (int i = 0; i < numRegions; i++) {

		regional_train[i].ComputeBlockingTimes(SignallingType, SetupTime, ReleaseTime, SightReacTime, AbsRTSupplement, PercRTSupplement);
	}

	// Print the Files
	PrintTrainBlockingTimes(OutputFolder);
}

// Function to compute Blocking Times of trains in mixed signalling areas
void ComputeBlockingTimesInMixedSignallingForAllTrains(double SetupTime, double ReleaseTime, double SightReacTime, double SafetyMargin, string OutputFolder, double AbsRTSupplement, double PercRTSupplement) {
	for (int i = 0; i < numRegions; i++) {

		regional_train[i].ComputeBlockingTimesInMixedSignallingAreas(SetupTime, ReleaseTime, SightReacTime, SafetyMargin, AbsRTSupplement, PercRTSupplement);
	}

	// Print the Files
	PrintTrainBlockingTimes(OutputFolder);
}

// Function that determines the correct departure headway to avoid conflicts (with this function we put blockingTime A always below Blocking Time blockSets)
double ComputeDepartureTimesToSolveConflicts(BlockingTimes A, BlockingTimes blockSets, double DepTimeTrain1, double DepTimeTrain2) {
	double ShiftedDepTime = -1;
	double overlap = -1;
	// with this function we move always train A below train blockSets
	overlap = blockSets.EndOccTime - A.StartOccTime;
	ShiftedDepTime = DepTimeTrain1 + overlap; // Add the overlap from the arrival distance

	return ShiftedDepTime;
}

double ComputeHWForLocationToDepartureTime(BlockingTimes A, BlockingTimes blockSets, double DepTimeTrain1, double DepTimeTrain2) {
	double Hw = -1;
	double overlap = -1;
	double DepartureTimeDistance = abs(DepTimeTrain1 - DepTimeTrain2);

	if (DepTimeTrain1 <= DepTimeTrain2) {	 // if train A enters the block before train blockSets
		if (A.EndOccTime > blockSets.StartOccTime) { // if the blocks overlap
			overlap = abs(A.EndOccTime - blockSets.StartOccTime);
			Hw = DepartureTimeDistance + overlap; // add the overlap to their arrival distance
		} else {								  // if the blocks do not overlap
			overlap = abs(A.EndOccTime - blockSets.StartOccTime);
			Hw = DepartureTimeDistance - overlap; // Subtract the overlap from the arrival distance
		}
	}

	else {									 // if train A departs after train blockSets
		if (blockSets.EndOccTime > A.StartOccTime) { // if the blocks overlap
			overlap = abs(blockSets.EndOccTime - A.StartOccTime);
			Hw = DepartureTimeDistance + overlap; // add the overlap to their arrival distance
		} else {								  // if the blocks do not overlap
			overlap = abs(blockSets.EndOccTime - A.StartOccTime);
			Hw = DepartureTimeDistance - overlap; // Subtract the overlap from the arrival distance
		}
	}
	return Hw;
}

// Function to Compute the Headway with another on a block section or a location
double ComputeHwForLocation(BlockingTimes A, BlockingTimes blockSets) {
	double Hw = -1;
	double overlap = -1;
	double ArrivalTimeDistance = abs(A.StartRunTime - blockSets.StartRunTime);

	if (A.StartRunTime <= blockSets.StartRunTime) {	 // if train A enters the block before train blockSets
		if (A.EndOccTime > blockSets.StartOccTime) { // if the blocks overlap
			overlap = abs(A.EndOccTime - blockSets.StartOccTime);
			Hw = ArrivalTimeDistance + overlap; // add the overlap to their arrival distance
		} else {								// if the blocks do not overlap
			overlap = abs(A.EndOccTime - blockSets.StartOccTime);
			Hw = ArrivalTimeDistance - overlap; // Subtract the overlap from the arrival distance
		}
	}

	else {									 // if train A enters the block after train blockSets
		if (blockSets.EndOccTime > A.StartOccTime) { // if the blocks overlap
			overlap = abs(blockSets.EndOccTime - A.StartOccTime);
			Hw = ArrivalTimeDistance + overlap; // add the overlap to their arrival distance
		} else {								// if the blocks do not overlap
			overlap = abs(blockSets.EndOccTime - A.StartOccTime);
			Hw = ArrivalTimeDistance - overlap; // Subtract the overlap from the arrival distance
		}
	}
	return Hw;
}

// Function to Compute the Headway with another on a block section or a location (in this function we move blocking time blockSets always below blocking time A)
double ComputeHwForLocationByShiftingBBelowA(BlockingTimes A, BlockingTimes blockSets) {
	double Hw = -1;
	double overlap = -1;
	// With this function we move always train A above train blockSets
	overlap = A.EndOccTime - blockSets.StartOccTime;
	Hw = abs(blockSets.StartRunTime - A.StartRunTime + overlap);

	return Hw;
}

// Function to Compute the Headway with another on a block section or a location (in this function we move blocking time blockSets always below blocking time A)
double ComputeShiftAtTimetablePointsByShiftingBBelowA(BlockingTimes A, BlockingTimes blockSets, double timeB) {
	double ShiftedTimeB = -1;
	double overlap = -1;
	// With this function we move always train A above train blockSets
	overlap = A.EndOccTime - blockSets.StartOccTime;
	ShiftedTimeB = timeB + overlap;

	return ShiftedTimeB;
}

// Debug Activate Signalling Function
void Debug_Activate_Signalling() {
	for (int t = 825; t < initial_variables.times; t++) {

		Occupy_Block_Sections_Of_Route(t); // Fill in the lists Blocks_Occupied and BlocksConnected

		releaseBlockConnections();  // Release Blocks connected with the one really occupied by a train
		activateSignallingSystem(); // Apply the rules of the signalling system for all the Blocks contained
		BlocksOccupied.clear();		  // Clear the list BlocksOccupied
		BlocksConnected.clear();	  // Clear the list BlocksConnected
	}
}

void DetectConflictsForAllTrains(Train* Trains, int numTrains) {
	for (int i = 0; i < numTrains; i++) {
		Trains[i].DetectConflictsWithPreviousDepartingTrains(Trains, numTrains);
	}
}

// Debugging Function
void Function_To_Debug_Occupation_Train(Train T, Section* BS, int Blocks) {
	for (int i = 825; i < T.End_Time; i++) {
		for (int h = 0; h < Blocks; h++) {
			if (((T.instant_spatial_position[i] - T.train_length) < BS[h].end_node.X * 1000) && ((T.instant_spatial_position[i] - T.train_length) >= BS[h].start_node.X * 1000)) {
				int Prev_Block = h - 1;
				if (h == 0)
					Prev_Block = 0;
				occupyBlockAndConnected(BS[h], BS[Prev_Block], (T.instant_spatial_position[i] - T.train_length), (T.instant_spatial_position[i - 1] - T.train_length));
				// if the Block Section has a switch in diverging position
				if (BS[h].withSwitchDiv == true)
					activateBlocksWithSwitchesDiv(BS[h], BS[Prev_Block].trackLineId, (T.instant_spatial_position[i] - T.train_length));
			}
			if ((T.instant_spatial_position[i] < BS[h].end_node.X * 1000) && (T.instant_spatial_position[i] >= BS[h].start_node.X * 1000)) {
				int Prev_Block = h - 1;
				if (h == 0)
					Prev_Block = 0;
				occupyBlockAndConnected(BS[h], BS[Prev_Block], (T.instant_spatial_position[i]), (T.instant_spatial_position[i - 1]));
				if (BS[h].withSwitchDiv == true) {
					activateBlocksWithSwitchesDiv(BS[h], BS[Prev_Block].trackLineId, T.instant_spatial_position[i]);
					break;
				}
			}
		}
		showElement(i, BlocksConnected);
		BlocksConnected.clear();
		BlocksOccupied.clear();
	}
}
// Function to Load All The OrderLists referred to Critical Nodes in the Infrastructure
void loadAllOrderLists(char* FolderName) {
	for (int i = 0; i < N_OrderLists; i++) {
		char OL_ID[20];
		snprintf(OL_ID, sizeof(OL_ID), "%d", i);
		string FileName;
		FileName = FileName + FolderName + "/OL" + OL_ID + ".txt";
		OL[i].Load_OrderList((char*)FileName.c_str());
	}
}

int Train::clampStationCount(int requested, const string& trainId) {
	if (requested <= kMaxTimetableStations)
		return requested;
	std::cerr << "Train " << trainId << " serves " << requested << " stations but the timetable arrays hold "
			  << kMaxTimetableStations << "; dropping the last " << requested - kMaxTimetableStations << " stops\n";
	return kMaxTimetableStations;
}
extern Logger owl;
// Function to Determine for each Route the Block Sections that are occupied by trains
//(This Function Fill in the list BlocksOccupied)
void Occupy_Block_Sections_Of_Route(int i) {
	for (int j = 0; j < numRegions; j++) {
		if ((regional_train[j].trainDescription == "B-Farum-HojeTaastrup_1-1") || (regional_train[j].trainDescription == "B-HojeTaastrup-Farum_2-1"))
			owl << "Train : " << regional_train[j].trainDescription << std::endl;
		regional_train[j].Det_Section_Occupied_By_Train(i, train_route[regional_train[j].indexOfRoute].sequence_of_block_sections, train_route[regional_train[j].indexOfRoute].N_Block_Sections);
	}
}

// Function to order blocking times by the StartOccTime
bool OrderByStartOccTime(BlockingTimes A, BlockingTimes blockSets) {
	if (A.StartOccTime <= blockSets.StartOccTime) {
		return true;
	} else {
		return false;
	}
}

// Function to Identify the point where following train under Virtual Coupling shall decpouple from the leading train
void Predict_And_Check_Decoupling_MA_For_All_Train_in_Following_Mode(int t) {
	for (int j = 0; j < numRegions; j++) {
		regional_train[j].Predict_And_Check_Validity_Of_MA_To_Split_At(t, regional_train, numRegions);
	}
}

// Function to Print out the blocking times of all the Trains
void PrintTimetablePoints(string MainFolder) {
	string FileName;
	FileName = FileName + MainFolder + "/TimetablePoints.txt";
	ofstream OutputFile;
	OutputFile.open((char*)FileName.c_str(), ios::binary);

	for (int i = 0; i < numRegions; i++) {
		OutputFile << regional_train[i].trainDescription << " " << regional_train[i].type << "\n";

		if (regional_train[i].TimetablePoints.empty() != 1) {
			for (list<TrainEvent>::iterator j = regional_train[i].TimetablePoints.begin(); j != regional_train[i].TimetablePoints.end(); j++) {
				OutputFile << j->SuccessorID << " ";
			}
		}
		OutputFile << "\n";

		if (regional_train[i].TimetablePoints.empty() != 1) {
			for (list<TrainEvent>::iterator j = regional_train[i].TimetablePoints.begin(); j != regional_train[i].TimetablePoints.end(); j++) {
				OutputFile << j->Position << " ";
			}
		}

		OutputFile << "\n";

		if (regional_train[i].TimetablePoints.empty() != 1) {
			for (list<TrainEvent>::iterator j = regional_train[i].TimetablePoints.begin(); j != regional_train[i].TimetablePoints.end(); j++) {
				OutputFile << j->Position << " ";
			}
		}

		OutputFile << "\n";

		if (regional_train[i].TimetablePoints.empty() != 1) {
			for (list<TrainEvent>::iterator j = regional_train[i].TimetablePoints.begin(); j != regional_train[i].TimetablePoints.end(); j++) {
				OutputFile << j->Time << " ";
			}
		}
		OutputFile << "\n";

		if (regional_train[i].TimetablePoints.empty() != 1) {
			for (list<TrainEvent>::iterator j = regional_train[i].TimetablePoints.begin(); j != regional_train[i].TimetablePoints.end(); j++) {
				OutputFile << j->Time2 << " ";
			}
		}
		OutputFile << "\n";
	}
}

// Function to Print out the blocking times of all the Trains
void PrintTrainBlockingTimes(string MainFolder) {
	string FileName;
	FileName = FileName + MainFolder + "/BlockingTimes.txt";
	ofstream OutputFile;
	OutputFile.open((char*)FileName.c_str(), ios::binary);

	for (int i = 0; i < numRegions; i++) {
		OutputFile << regional_train[i].trainDescription << "\n";

		for (int j = 0; j < regional_train[i].N_BlockTimeComplete; j++) {
			OutputFile << regional_train[i].BlockTime[j].BlockID << " ";
		}
		OutputFile << "\n";

		for (int j = 0; j < regional_train[i].N_BlockTimeComplete; j++) {
			/*if (train_route[regional_train[i].indexOfRoute].reversed_direction==0)
			OutputFile<<regional_train[i].BlockTime[j].PosStart<<" ";
			else
			OutputFile<<train_route[regional_train[i].indexOfRoute].OriginalRefReversedRoute-regional_train[i].BlockTime[j].PosEnd<<" ";*/
			OutputFile << regional_train[i].BlockTime[j].GeoPosStart << " ";
		}

		OutputFile << "\n";

		for (int j = 0; j < regional_train[i].N_BlockTimeComplete; j++) {
			/*if (train_route[regional_train[i].indexOfRoute].reversed_direction==0)
			OutputFile<<regional_train[i].BlockTime[j].PosEnd<<" ";
			else
			OutputFile<<train_route[regional_train[i].indexOfRoute].OriginalRefReversedRoute-regional_train[i].BlockTime[j].PosStart<<" ";*/
			OutputFile << regional_train[i].BlockTime[j].GeoPosEnd << " ";
		}

		OutputFile << "\n";

		for (int j = 0; j < regional_train[i].N_BlockTimeComplete; j++) {
			OutputFile << regional_train[i].BlockTime[j].StartOccTime << " ";
		}
		OutputFile << "\n";

		for (int j = 0; j < regional_train[i].N_BlockTimeComplete; j++) {
			OutputFile << regional_train[i].BlockTime[j].EndOccTime << " ";
		}
		OutputFile << "\n";
	}
}

// This Function is to Print out the LastEnteredTrain of the list OL
void Print_Out_OL_LastEntry(OrderList OL, int t) {
	ofstream output;
	string LastEntryName;
	LastEntryName = LastEntryName + InputMainFolder + "/Routes/OL_LastEntry.txt";
	output.open((char*)LastEntryName.c_str(), ios::app);
	output << t << " " << OL.LastEnteredTrain << "\n";
	output.close();
}

// Function to determine the scheduled sequence of train departure from a certain Node and print it on a text file
void Print_Scheduled_Dep_Order_In_Node(double Node_X) { // Node_X must be expressed in Km
	list<TrainEvent> Train_Order_in_X;
	for (int i = 0; i < numRegions; i++) {
		if (regional_train[i].Start_Node_X == Node_X * 1000) {
			TrainEvent TEL;
			TEL.trainDescription = regional_train[i].trainDescription;
			TEL.Time = regional_train[i].departure_time;
			Train_Order_in_X.push_back(TEL);
		} else {
			for (int t = 1; t < regional_train[i].End_Time; t++) {
				if ((regional_train[i].instant_spatial_position[t - 1] <= Node_X * 1000) && (regional_train[i].instant_spatial_position[t] >= Node_X * 1000)) {
					TrainEvent TEL;
					TEL.trainDescription = regional_train[i].trainDescription;
					TEL.Time = t;
					Train_Order_in_X.push_back(TEL);
				}
			}
		}
	}
	// Ordering the TrainEvent List chronologically
	Train_Order_in_X.sort(orderTrainEvents);
	ofstream ScheduledTrainOrder;
	string FileName;

	char nodex[20];
	snprintf(nodex, sizeof(nodex), "%f", Node_X);
	FileName = FileName + InputMainFolder + "/TrackLines/ScheduledOrder_NodeX_" + nodex + ".txt";
	ScheduledTrainOrder.open((char*)FileName.c_str());
	ScheduledTrainOrder << Node_X << "\n";
	list<TrainEvent>::iterator it;
	if (Train_Order_in_X.empty() != 1) {
		for (it = Train_Order_in_X.begin(); it != Train_Order_in_X.end(); it++)
			ScheduledTrainOrder << it->trainDescription << "\n";
	}
}

// Print EventTimes in the Format for TU Delft Timetabling tool
void Print_Timetabling_Point_TUDelft_format(string MainFolder) {

	int IDEvent1 = 0, IDEvent2 = 0;
	double TimeEvent1 = 0, TimeEvent2 = 0;
	double ProcessTimeEvents_1_2 = 0;
	TrainEvent LastWrittenEvent; // this TrainList memorises the information relative to the last event written down

	// Initialising the File of the Events
	string FileOutName;
	FileOutName = FileOutName + MainFolder + "/Events.csv";

	ofstream FILEOUTPUT;

	FILEOUTPUT.open((char*)FileOutName.c_str(), ios::binary);

	FILEOUTPUT << "event_id,line_id,hourlyrunno,direction,segmentno,station_abbr,farstation_abbr,event_type,time,track\n";

	// Initialising the File of the Processes
	string FileProcessOUT;
	FileProcessOUT = FileProcessOUT + MainFolder + "/Processes.csv";
	ofstream FILEPROCESS;
	FILEPROCESS.open((char*)FileProcessOUT.c_str(), ios::binary);

	FILEPROCESS << "from_event_id,to_event_id,mindur,scheddur,maxdur,process_type\n";

	int EventCounter = 0; // This is the Event Counter

	// Filling in the events
	for (int i = 0; i < numRegions; i++) {

		if (regional_train[i].TimetablePoints.empty() != 1) {
			for (list<TrainEvent>::iterator t = regional_train[i].TimetablePoints.begin(); t != regional_train[i].TimetablePoints.end(); t++) {

				string StationAbbr, FarStationArrivalEvent, FarStationDepartureEvent, Event_TypeArrival, Event_TypeDeparture;
				int TimeArrival, TimeDeparture;

				// specifying Station Abbr
				StationAbbr = t->SuccessorID;
				list<TrainEvent>::iterator BEG = regional_train[i].TimetablePoints.begin(); // Defining the position of the first station
				list<TrainEvent>::iterator END = regional_train[i].TimetablePoints.end();
				END--; // Defining the position of the last station
				// Defining the FarStation
				if ((t != BEG) && (t != END)) {
					list<TrainEvent>::iterator StationBefore = t;
					StationBefore--; // Setting Station before t
					FarStationArrivalEvent = StationBefore->SuccessorID;

					list<TrainEvent>::iterator StationAfter = t;
					StationAfter++; // Setting the station after t
					FarStationDepartureEvent = StationAfter->SuccessorID;
				}

				else if (t == BEG) {
					FarStationArrivalEvent = "START";
					list<TrainEvent>::iterator StationAfter = t;
					if (t != END) {

						StationAfter++;										  // Increase the pointer only if current station is not the last one
						FarStationDepartureEvent = StationAfter->SuccessorID; // In this case the Far Station Departure Event becomes the next station
					}

					else { // else put END as Far Station since the train departs from the last station to go to END
						FarStationDepartureEvent = "END";
					}

				}

				else if (t == END) {
					FarStationDepartureEvent = "END";
					list<TrainEvent>::iterator StationBefore = t;
					if (t != BEG) {
						StationBefore--; // Take the he Station Before t if t is not also the beginning
						FarStationArrivalEvent = StationBefore->SuccessorID;
					}

					else { // The train will depart from Start if t is also the first Station
						FarStationArrivalEvent = "START";
					}
				}

				// Assigning the type of event
				if (t->Time == t->Time2) {
					Event_TypeArrival = "2-arrthru";
					Event_TypeDeparture = "1-depthru";
				} else {
					Event_TypeArrival = "3-arr";
					Event_TypeDeparture = "0-dep";
				}

				// Assigning the times
				TimeArrival = (int)t->Time;
				TimeDeparture = (int)t->Time2;

				// Writing down the arrival event
				if (FarStationArrivalEvent != "START") {

					EventCounter++; // Increase the event counter of one unit
					FILEOUTPUT << EventCounter << "," << t->trainDescription << "," << 1 << "," << 1 << "," << 1 << "," << StationAbbr << "," << FarStationArrivalEvent << "," << Event_TypeArrival << "," << TimeArrival << ", \n";

					if ((t->trainDescription == LastWrittenEvent.trainDescription) && (LastWrittenEvent.Position != -10000)) { // IF this is an event of the same train and the last written Event has been already initialised

						if (StationAbbr == LastWrittenEvent.SuccessorID) { // if the station of this event is the same of the previous event
							if (TimeArrival == LastWrittenEvent.Time) {	   // if the times are the same then it is a thru process
								FILEPROCESS << LastWrittenEvent.Position << "," << EventCounter << "," << TimeArrival - LastWrittenEvent.Time << ", , ," << "thru" << "\n";
							} else { // else the process is a dwell
								FILEPROCESS << LastWrittenEvent.Position << "," << EventCounter << "," << TimeArrival - LastWrittenEvent.Time << ", , ," << "dwell" << "\n";
							}
						} else { // if the Station of the current event is different from the station of the previous event then the process is a run
							FILEPROCESS << LastWrittenEvent.Position << "," << EventCounter << "," << TimeArrival - LastWrittenEvent.Time << ", , ," << "run" << "\n";
						}
					}

					// Setting the process times
					LastWrittenEvent.trainDescription = t->trainDescription;
					LastWrittenEvent.SuccessorID = StationAbbr;
					LastWrittenEvent.Time = TimeArrival;
					LastWrittenEvent.Position = EventCounter;
				}

				// Writing down the departure event
				if (FarStationDepartureEvent != "END") {

					EventCounter++; // Increase the event counter of one unit
					FILEOUTPUT << EventCounter << "," << t->trainDescription << "," << 1 << "," << 1 << "," << 1 << "," << StationAbbr << "," << FarStationDepartureEvent << "," << Event_TypeDeparture << "," << TimeDeparture << " \n";

					if ((t->trainDescription == LastWrittenEvent.trainDescription) && (LastWrittenEvent.Position != -10000)) { // IF this is an event of the same train and the last written Event has been already initialised

						if (StationAbbr == LastWrittenEvent.SuccessorID) { // if the station of this event is the same of the previous event
							if (TimeDeparture == LastWrittenEvent.Time) {  // if the times are the same then it is a thru process
								FILEPROCESS << LastWrittenEvent.Position << "," << EventCounter << "," << TimeDeparture - LastWrittenEvent.Time << ", , ," << "thru" << "\n";
							} else { // else the process is a dwell
								FILEPROCESS << LastWrittenEvent.Position << "," << EventCounter << "," << TimeDeparture - LastWrittenEvent.Time << ", , ," << "dwell" << "\n";
							}
						} else { // if the Station of the current event is different from the station of the previous event then the process is a run
							FILEPROCESS << LastWrittenEvent.Position << "," << EventCounter << "," << TimeDeparture - LastWrittenEvent.Time << ", , ," << "run" << "\n";
						}
					}

					// Setting the process times
					LastWrittenEvent.trainDescription = t->trainDescription;
					LastWrittenEvent.SuccessorID = StationAbbr;
					LastWrittenEvent.Time = TimeDeparture;
					LastWrittenEvent.Position = EventCounter;
				}
			}
		}
	}
}

// Function to report the position of the trains in ETCS Level 3
void ReportAllTrainPositionsToRBC(int i, double ETCS3SafetyMargin) {
	for (int j = 0; j < numRegions; j++) {
		regional_train[j].ReportPositionToRBC(i, train_route[regional_train[j].indexOfRoute].sequence_of_block_sections, train_route[regional_train[j].indexOfRoute].N_Block_Sections, ETCS3SafetyMargin);
	}
}

// Function to Reset the Lists that will be updated at each instant
void resetOlToUpdate() {
	for (int i = 0; i < N_OrderLists; i++) {
		for (int j = 0; j < OL[i].numTeList; j++) {
			OL[i].TE[j].trainDescription = "None";
		}
	}
}

// Function to Assign to OL0 the same order of OL1
void Set_OL0_as_OL1() {
	for (int j = 0; j < OL[0].numTeList; j++) {
		OL[0].TE[j].trainDescription = "None";
	}
	OL[0].numTeList = OL[1].numTeList; // Setting numTeList of OL0 and OL1 as equal
	for (int j = 0; j < OL[0].numTeList; j++) {
		OL[0].TE[j].trainDescription = OL[1].TE[j].trainDescription;
	}
}

// Function to Set the Critical Nodes In which a special order has to be respected and Link these nodes to the respective OrderLists OL
void setRespectOrderAndIndexOrderList() {
	for (int i = 0; i < N_OrderLists; i++) {
		for (int j = 0; j < numTrackLines; j++) {
			if (blockSets[j].member[0].startNode.X == OL[i].Node_X) {
				blockSets[j].member[0].startNode.respectOrder = true;
				blockSets[j].member[0].startNode.indexOrderList = i;
			}
			for (int h = 0; h < blockSets[j].len; h++) {
				if (blockSets[j].member[h].endNode.X == OL[i].Node_X) {
					blockSets[j].member[h].endNode.respectOrder = true;
					blockSets[j].member[h].endNode.indexOrderList = i;
				}
			}
		}
	}
}

// This is the function to set the departure sequence of trains at checkpoint nodes of a block section
void Set_RespectOrder_And_Index_OrderList_Upgraded() {
	for (int i = 0; i < N_OrderLists; i++) {
		for (int j = 0; j < Blocks; j++) {
			if ((signalling_block_sections[j].ID == OL[i].BlockID) && (signalling_block_sections[j].start_node.X == OL[i].Node_X)) { // Here we are assigning the train sequence to Block Section when Routes have not been defined yet. So we do not need to distinguish between reversed and not reversed block sections because Block sections nodes all have absolute coordinates and not relative. So the code can be written jut for the absolute coordinates of Block Section nodes
				signalling_block_sections[j].start_node.respectOrder = true;
				signalling_block_sections[j].start_node.indexOrderList = i;
				// Assigning the same things to the beginning Node of the first arcs_in_signalling_block_section (i.e. arcs_in_signalling_block_section[0]) of signalling_block_sections[j]
				signalling_block_sections[j].arcs_in_signalling_block_section[0].startNode.respectOrder = true;
				signalling_block_sections[j].arcs_in_signalling_block_section[0].startNode.indexOrderList = i;
			}
			for (int h = 0; h < signalling_block_sections[j].total_arcs; h++) {
				if ((signalling_block_sections[j].ID == OL[i].BlockID) && (signalling_block_sections[j].arcs_in_signalling_block_section[h].endNode.X == OL[i].Node_X)) {
					signalling_block_sections[j].arcs_in_signalling_block_section[h].endNode.respectOrder = true;
					signalling_block_sections[j].arcs_in_signalling_block_section[h].endNode.indexOrderList = i;
				}
			}
		}
	}
}

// This is the function to set the departure sequence of trains at checkpoint nodes of a block section
void Set_RespectOrder_And_Index_OrderList_Upgraded_Improved() {
	for (int i = 0; i < N_OrderLists; i++) {
		for (int j = 0; j < Blocks; j++) {
			if (signalling_block_sections[j].start_node.X == OL[i].Node_X) { // Here we are assigning the train sequence to Block Section when Routes have not been defined yet. So we do not need to distinguish between reversed and not reversed block sections because Block sections nodes all have absolute coordinates and not relative. So the code can be written jut for the absolute coordinates of Block Section nodes
				signalling_block_sections[j].start_node.respectOrder = true;
				signalling_block_sections[j].start_node.indexOrderList = i;
				// Assigning the same things to the beginning Node of the first arcs_in_signalling_block_section (i.e. arcs_in_signalling_block_section[0]) of signalling_block_sections[j]
				signalling_block_sections[j].arcs_in_signalling_block_section[0].startNode.respectOrder = true;
				signalling_block_sections[j].arcs_in_signalling_block_section[0].startNode.indexOrderList = i;
			}

			if (signalling_block_sections[j].end_node.X == OL[i].Node_X) { // Here we are assigning the train sequence to Block Section when Routes have not been defined yet. So we do not need to distinguish between reversed and not reversed block sections because Block sections nodes all have absolute coordinates and not relative. So the code can be written jut for the absolute coordinates of Block Section nodes
				signalling_block_sections[j].end_node.respectOrder = true;
				signalling_block_sections[j].end_node.indexOrderList = i;
			}

			for (int h = 0; h < signalling_block_sections[j].total_arcs; h++) {
				if (signalling_block_sections[j].arcs_in_signalling_block_section[h].endNode.X == OL[i].Node_X) {
					signalling_block_sections[j].arcs_in_signalling_block_section[h].endNode.respectOrder = true;
					signalling_block_sections[j].arcs_in_signalling_block_section[h].endNode.indexOrderList = i;
				}
			}
		}
	}
}

// This is the function to set the departure sequence of trains at checkpoint nodes of a block section considering whether the OrderList refers to a Merging or a diverging Junction.
//  Whether the OL refers to a Merging or Diverging Junction shall be evaluated looking at the shape of the junction following the Km progressive in the same direction of
// IMPORTANT NOTE: When applying the function below the OL point should be coinciding with the last switch of a merging junction or the very first switch of a diverging junction (looking at the infrastructure layout when following the same abscissa direction which was used to intialise the infrastructure)

void Set_RespectOrder_And_Index_OrderList_Upgraded_Improved_With_JunctionType() {
	for (int i = 0; i < N_OrderLists; i++) {
		for (int j = 0; j < Blocks; j++) {
			if (OL[i].Is_MergingJunction == 1) {
				if ((signalling_block_sections[j].start_node.X < OL[i].Node_X) && (signalling_block_sections[j].end_node.X >= OL[i].Node_X)) {
					// if the entry signal is a normal signal then set that signal to reference point where the Order List shild be respected
					if (signalling_block_sections[j].start_node.virtualSignal == 0) {
						// Assigning the respect order conditions to the initial Node of this block section which has the OL Node X inside its length. This means that the OL Order will be respected at the entry signal of this block section or ( which is the same) at the exist signal of the previous block sections
						// Respect order cannot be assigned to virtual signals as those should not exist in reality and trains cannot wait in front of a double switch for being reodered
						signalling_block_sections[j].start_node.respectOrder = true;
						signalling_block_sections[j].start_node.indexOrderList = i;
						// Assigning the same things to the beginning Node of the first arcs_in_signalling_block_section (i.e. arcs_in_signalling_block_section[0]) of signalling_block_sections[j]
						signalling_block_sections[j].arcs_in_signalling_block_section[0].startNode.respectOrder = true;
						signalling_block_sections[j].arcs_in_signalling_block_section[0].startNode.indexOrderList = i;
						double SignalNodeID = signalling_block_sections[j].arcs_in_signalling_block_section[0].startNode.ID;
						double SignalNodeX = signalling_block_sections[j].arcs_in_signalling_block_section[0].startNode.X;
						string SignalNodeTDSB_ID = signalling_block_sections[j].arcs_in_signalling_block_section[0].startNode.tdsbId;
						// Check now for all the block sections which end with the such a signal they might be either block sections on the same trackline of the identified block or not
						// To those block sections we need to assign the respect order conditions to the end Node of the last Arc of the block
						for (int k = 0; k < Blocks; k++) {
							int N_TotarcsINBlock = signalling_block_sections[k].total_arcs;
							// Impose the respect order also the exit signal of block sections whose exit signals coincides with the entry signal of the block section identified in the piece of code above, simply because the exit signal of this block section and the entry signal of the previous block section are the same signal
							if ((signalling_block_sections[k].arcs_in_signalling_block_section[N_TotarcsINBlock - 1].endNode.ID == SignalNodeID) && (signalling_block_sections[k].arcs_in_signalling_block_section[N_TotarcsINBlock - 1].endNode.X == SignalNodeX) && (signalling_block_sections[k].arcs_in_signalling_block_section[N_TotarcsINBlock - 1].endNode.tdsbId == SignalNodeTDSB_ID)) {
								// This is imposed to the end Node of the last Arc of the block section
								signalling_block_sections[k].arcs_in_signalling_block_section[N_TotarcsINBlock - 1].endNode.respectOrder = true;
								signalling_block_sections[k].arcs_in_signalling_block_section[N_TotarcsINBlock - 1].endNode.indexOrderList = i;
								// As well as to the last Node of the block section itself
								signalling_block_sections[k].end_node.respectOrder = true;
								signalling_block_sections[k].end_node.indexOrderList = i;
							}
							// Impose also to respect the order at all of those blocks whose starting Node coincides with the identified non-virtual signal
							if ((signalling_block_sections[k].arcs_in_signalling_block_section[0].startNode.ID == SignalNodeID) && (signalling_block_sections[k].arcs_in_signalling_block_section[0].startNode.X == SignalNodeX) && (signalling_block_sections[k].arcs_in_signalling_block_section[0].startNode.tdsbId == SignalNodeTDSB_ID)) {
								// This is imposed to the end Node of the last Arc of the block section
								signalling_block_sections[k].arcs_in_signalling_block_section[0].startNode.respectOrder = true;
								signalling_block_sections[k].arcs_in_signalling_block_section[0].startNode.indexOrderList = i;
								// As well as to the last Node of the block section itself
								signalling_block_sections[k].start_node.respectOrder = true;
								signalling_block_sections[k].start_node.indexOrderList = i;
							}
						}

					} else { // if instead the entry signal of that block section is a virtual signal then the Order List should be respected at the start signal of the previous block section ( i.e. the one having the virtual signal as exit signal)

						double VirtualSignalID = signalling_block_sections[j].start_node.ID;
						double VirtualSignalX = signalling_block_sections[j].start_node.X;
						// Check if the Virtual signal also have a tdsbId which is different than "None". In that case also the condition of equal on the tdsbId should be put in the if condition below
						string VirtualSignalTDSB_ID = signalling_block_sections[j].start_node.tdsbId;

						// defining the Signal ID and Signal Node X of the non-virtual entry signal of the block section ending with the identified Virtual Signal
						double SignalNodeID = -1;
						double SignalNodeX = -1;
						string SignalNodeTDSB_ID = "None";
						// Iterate through the block section to find the block section which has a diverging switch and has the exit signal equal to the Virtual Signal identified
						for (int z = 0; z < Blocks; z++) {

							if ((signalling_block_sections[z].end_node.X == VirtualSignalX) && (signalling_block_sections[z].end_node.ID == VirtualSignalID) && (signalling_block_sections[z].end_node.tdsbId == VirtualSignalTDSB_ID) && (signalling_block_sections[z].withSwitchDiv == 1)) {

								signalling_block_sections[z].start_node.respectOrder = true;
								signalling_block_sections[z].start_node.indexOrderList = i;
								// Assigning the same things to the beginning Node of the first arcs_in_signalling_block_section (i.e. arcs_in_signalling_block_section[0]) of signalling_block_sections[z]
								signalling_block_sections[z].arcs_in_signalling_block_section[0].startNode.respectOrder = true;
								signalling_block_sections[z].arcs_in_signalling_block_section[0].startNode.indexOrderList = i;
								SignalNodeID = signalling_block_sections[z].arcs_in_signalling_block_section[0].startNode.ID;
								SignalNodeX = signalling_block_sections[z].arcs_in_signalling_block_section[0].startNode.X;
								SignalNodeTDSB_ID = signalling_block_sections[z].arcs_in_signalling_block_section[0].startNode.tdsbId;
								break; // break the for loop of iterator z iterating through the Blocks
							}
						}
						// if a real non-virtual entry signal of the block section ending with the identified Virtual Signal exists
						// then assign the Respect order also to all of the previous block sections which exit with such a non-virtual signal (i.e. having exit signal coinciding with such a non-virtual signal)
						if ((SignalNodeID != -1) && (SignalNodeX != -1) && (SignalNodeTDSB_ID != "None")) {
							// Check now for all the block sections which end with the such a non non-virtual signal Signal Node which might be either block sections on the same trackline of the identified block or not
							// To those block sections we need to assign the respect order conditions to the end Node of the last Arc of the block
							for (int k = 0; k < Blocks; k++) {
								int N_TotarcsINBlock = signalling_block_sections[k].total_arcs;
								// Impose the respect order also the exit signal of block sections whose exit signals coincides with the entry signal of the block section identified in the piece of code above, simply because the exit signal of this block section and the entry signal of the previous block section are the same signal
								if ((signalling_block_sections[k].arcs_in_signalling_block_section[N_TotarcsINBlock - 1].endNode.ID == SignalNodeID) && (signalling_block_sections[k].arcs_in_signalling_block_section[N_TotarcsINBlock - 1].endNode.X == SignalNodeX) && (signalling_block_sections[k].arcs_in_signalling_block_section[N_TotarcsINBlock - 1].endNode.tdsbId == SignalNodeTDSB_ID)) {
									// This is imposed to the end Node of the last Arc of the block section
									signalling_block_sections[k].arcs_in_signalling_block_section[N_TotarcsINBlock - 1].endNode.respectOrder = true;
									signalling_block_sections[k].arcs_in_signalling_block_section[N_TotarcsINBlock - 1].endNode.indexOrderList = i;
									// As well as to the last Node of the block section itself
									signalling_block_sections[k].end_node.respectOrder = true;
									signalling_block_sections[k].end_node.indexOrderList = i;
								}
								// Impose also to respect the order at all of those blocks whose starting Node coincides with the identified non-virtual signal
								if ((signalling_block_sections[k].arcs_in_signalling_block_section[0].startNode.ID == SignalNodeID) && (signalling_block_sections[k].arcs_in_signalling_block_section[0].startNode.X == SignalNodeX) && (signalling_block_sections[k].arcs_in_signalling_block_section[0].startNode.tdsbId == SignalNodeTDSB_ID)) {
									// This is imposed to the end Node of the last Arc of the block section
									signalling_block_sections[k].arcs_in_signalling_block_section[0].startNode.respectOrder = true;
									signalling_block_sections[k].arcs_in_signalling_block_section[0].startNode.indexOrderList = i;
									// As well as to the last Node of the block section itself
									signalling_block_sections[k].start_node.respectOrder = true;
									signalling_block_sections[k].start_node.indexOrderList = i;
								}
							}
						}
					}
				}

			}

			// If the OL order List refers to a Merging Junction ( looking at the the infrastructure layout when following teh progressive direction by which the infrastructure is defined in EGTRAIN)
			// Then the order should be assigned to the exit signal of those block section which have the OL location inside their lengths, as well as to the entry signal of the immediately adjacent block section ( whose entry signals coincide with the exist signal of the identified block)
			else if (OL[i].Is_DivergingJunction == 1) {
				if ((signalling_block_sections[j].start_node.X < OL[i].Node_X) && (signalling_block_sections[j].end_node.X >= OL[i].Node_X)) {

					if (signalling_block_sections[j].end_node.virtualSignal == 0) {
						// Assigning the respect order conditions to the end Node (i.e. the exit signal) of this block section which has the OL Node X inside its length. This means that the OL Order will be respected at the entry signal of this block section or ( which is the same) at the exist signal of the previous block sections
						// Respect order cannot be assigned to virtual signals as those should not exist in reality and trains cannot wait in front of a double switch for being reodered
						signalling_block_sections[j].end_node.respectOrder = true;
						signalling_block_sections[j].end_node.indexOrderList = i;
						// Assigning the same things to the end Node of the last Arc of the block section
						int N_TotarcsINBlock = signalling_block_sections[j].total_arcs;
						signalling_block_sections[j].arcs_in_signalling_block_section[N_TotarcsINBlock - 1].endNode.respectOrder = true;
						signalling_block_sections[j].arcs_in_signalling_block_section[N_TotarcsINBlock - 1].endNode.indexOrderList = i;
						double SignalNodeID = signalling_block_sections[j].arcs_in_signalling_block_section[N_TotarcsINBlock - 1].endNode.ID;
						double SignalNodeX = signalling_block_sections[j].arcs_in_signalling_block_section[N_TotarcsINBlock - 1].endNode.X;
						string SignalNodeTDSB_ID = signalling_block_sections[j].arcs_in_signalling_block_section[N_TotarcsINBlock - 1].endNode.tdsbId;
						// Check now for all the block sections which start with the such a signal they might be either block sections on the same trackline of the identified block or not
						// To those block sections we need to assign the respect order conditions to the first Node of the first Arc of the block
						for (int k = 0; k < Blocks; k++) {

							// Impose the respect order also the entry signal of block sections whose entry signals coincides with the exit signal of the block section identified in the piece of code above, simply because the exit signal of this block section and the entry signal of the previous block section are the same signal
							if ((signalling_block_sections[k].arcs_in_signalling_block_section[0].startNode.ID == SignalNodeID) && (signalling_block_sections[k].arcs_in_signalling_block_section[0].startNode.X == SignalNodeX) && (signalling_block_sections[k].arcs_in_signalling_block_section[0].startNode.tdsbId == SignalNodeTDSB_ID)) {
								// This is imposed to the end Node of the last Arc of the block section
								signalling_block_sections[k].arcs_in_signalling_block_section[0].startNode.respectOrder = true;
								signalling_block_sections[k].arcs_in_signalling_block_section[0].startNode.indexOrderList = i;
								// As well as to the start Node (i.e. the actual entry signal Node) of the block section itself
								signalling_block_sections[k].start_node.respectOrder = true;
								signalling_block_sections[k].start_node.indexOrderList = i;
							}

							int N_TotarcsINBlock = signalling_block_sections[k].total_arcs;
							// Impose the respect order also the exit signal of block sections whose exit signals coincides with the entry signal of the block section identified in the piece of code above, simply because the exit signal of this block section and the entry signal of the previous block section are the same signal
							if ((signalling_block_sections[k].arcs_in_signalling_block_section[N_TotarcsINBlock - 1].endNode.ID == SignalNodeID) && (signalling_block_sections[k].arcs_in_signalling_block_section[N_TotarcsINBlock - 1].endNode.X == SignalNodeX) && (signalling_block_sections[k].arcs_in_signalling_block_section[N_TotarcsINBlock - 1].endNode.tdsbId == SignalNodeTDSB_ID)) {
								// This is imposed to the end Node of the last Arc of the block section
								signalling_block_sections[k].arcs_in_signalling_block_section[N_TotarcsINBlock - 1].endNode.respectOrder = true;
								signalling_block_sections[k].arcs_in_signalling_block_section[N_TotarcsINBlock - 1].endNode.indexOrderList = i;
								// As well as to the last Node of the block section itself
								signalling_block_sections[k].end_node.respectOrder = true;
								signalling_block_sections[k].end_node.indexOrderList = i;
							}
						}
					}

					else { // if instead the exit signal of that block section is a virtual signal then the Order List should be respected at the exit signal of the next block section ( i.e. the one having the identified virtual signal as entry signal)

						double VirtualSignalID = signalling_block_sections[j].end_node.ID;
						double VirtualSignalX = signalling_block_sections[j].end_node.X;
						// if the Virtual Signal has a tdsbId then we also need to use its tdsbId in the if condition below otherwise only the two features below could suffice (although some issue might arise with nodes having the same X and ID on other tracklines)
						string VirtualSignalTDSB_ID = signalling_block_sections[j].end_node.tdsbId;

						// defining the Signal ID and Signal Node X of the non-virtual exit signal of the block section starting with the identified Virtual Signal
						double SignalNodeID = -1;
						double SignalNodeX = -1;
						string SignalNodeTDSB_ID = "None";

						// Iterate through the block section to find the block section which has a diverging switch and has the exit signal equal to the Virtual Signal identified
						for (int z = 0; z < Blocks; z++) {

							if ((signalling_block_sections[z].start_node.X == VirtualSignalX) && (signalling_block_sections[z].start_node.ID == VirtualSignalID) && (signalling_block_sections[z].start_node.tdsbId == VirtualSignalTDSB_ID) && (signalling_block_sections[z].withSwitchDiv == 1)) {

								signalling_block_sections[z].end_node.respectOrder = true;
								signalling_block_sections[z].end_node.indexOrderList = i;

								// Assigning the same things to the end Node of the last arcs_in_signalling_block_section (i.e. arcs_in_signalling_block_section[NTotarcsInBlock-1]) of signalling_block_sections[z]

								int N_TotarcsINBlock = signalling_block_sections[z].total_arcs;
								signalling_block_sections[z].arcs_in_signalling_block_section[N_TotarcsINBlock - 1].endNode.respectOrder = true;
								signalling_block_sections[z].arcs_in_signalling_block_section[N_TotarcsINBlock - 1].endNode.indexOrderList = i;
								SignalNodeID = signalling_block_sections[z].arcs_in_signalling_block_section[N_TotarcsINBlock - 1].endNode.ID;
								SignalNodeX = signalling_block_sections[z].arcs_in_signalling_block_section[N_TotarcsINBlock - 1].endNode.X;
								SignalNodeTDSB_ID = signalling_block_sections[z].arcs_in_signalling_block_section[N_TotarcsINBlock - 1].endNode.tdsbId;
								break; // break the for loop of iterator z iterating through the Blocks
							}
						}
						// if a real non-virtual exit signal of the block section starting with the identified Virtual Signal exists
						// then assign the Respect order also to all of the next block sections which start with such a non-virtual signal (i.e. having entry signal coinciding with such a non-virtual signal)
						if ((SignalNodeID != -1) && (SignalNodeX != -1) && (SignalNodeTDSB_ID != "None")) {
							// Check now for all the block sections which start with the such a non-virtual signal which might be either block sections on the same trackline of the identified block or not
							// To those block sections we need to assign the respect order conditions to the start Node of the first Arc of the block
							for (int k = 0; k < Blocks; k++) {

								// Impose the respect order also the entry signal of block sections whose entry signals coincides with the exit signal of the block section identified in the piece of code above, simply because the exit signal of this block section and the entry signal of the previous block section are the same signal
								if ((signalling_block_sections[k].arcs_in_signalling_block_section[0].startNode.ID == SignalNodeID) && (signalling_block_sections[k].arcs_in_signalling_block_section[0].startNode.X == SignalNodeX) && (signalling_block_sections[k].arcs_in_signalling_block_section[0].startNode.tdsbId == SignalNodeTDSB_ID)) {
									// This is imposed to the end Node of the last Arc of the block section
									signalling_block_sections[k].arcs_in_signalling_block_section[0].startNode.respectOrder = true;
									signalling_block_sections[k].arcs_in_signalling_block_section[0].startNode.indexOrderList = i;
									// As well as to the start Node (i.e. the actual entry signal Node) of the block section itself
									signalling_block_sections[k].start_node.respectOrder = true;
									signalling_block_sections[k].start_node.indexOrderList = i;
								}

								int N_TotarcsINBlock = signalling_block_sections[k].total_arcs;
								// Impose the respect order also the exit signal of block sections whose exit signals coincides with the entry signal of the block section identified in the piece of code above, simply because the exit signal of this block section and the entry signal of the previous block section are the same signal
								if ((signalling_block_sections[k].arcs_in_signalling_block_section[N_TotarcsINBlock - 1].endNode.ID == SignalNodeID) && (signalling_block_sections[k].arcs_in_signalling_block_section[N_TotarcsINBlock - 1].endNode.X == SignalNodeX) && (signalling_block_sections[k].arcs_in_signalling_block_section[N_TotarcsINBlock - 1].endNode.tdsbId == SignalNodeTDSB_ID)) {
									// This is imposed to the end Node of the last Arc of the block section
									signalling_block_sections[k].arcs_in_signalling_block_section[N_TotarcsINBlock - 1].endNode.respectOrder = true;
									signalling_block_sections[k].arcs_in_signalling_block_section[N_TotarcsINBlock - 1].endNode.indexOrderList = i;
									// As well as to the last Node of the block section itself
									signalling_block_sections[k].end_node.respectOrder = true;
									signalling_block_sections[k].end_node.indexOrderList = i;
								}
							}
						}
					}
				}
			}
		}
	}
}

// check train arrival/departure at/from destination/origin
void Train::checkTrainArrDep(int trainIdx, int t) {
	// add X to last station if needed (initialized as 0 in the beginning)
	if (Stations[numStations - 1].X == 0) {
		// find station position
		for (int j = 0; j < numStations; j++) {
			if (Stations[numStations - 1].stationName == StationArray[j].stationName) {
				Stations[numStations - 1].X = StationArray[j].X;
				break;
			}
		}
	}

	// add X to fisrt station if needed (initialized as 0 in the beginning)
	if (Stations[0].X == 0) {
		// find station position
		for (int j = 0; j < numStations; j++) {
			if (Stations[0].stationName == StationArray[j].stationName) {
				Stations[0].X = StationArray[j].X;
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
		cout << "\n<<< " << trainDescription << " ARRIVED at " << Stations[numStations - 1].stationName << " at t = " << t << " - " << simulationTime(t, initial_variables.startingSimulationTime) << endl;
	}
	// train departing from origin
	else if (((std::fabs(X - Stations[0].X) < 0.001) || (std::fabs(std::fabs(X - Stations[0].X) - train_length / 1000) < 0.001)) && (t >= 1) && (instant_train_speed[t - 1] == 0) && (instant_train_speed[t] != 0)) { // position tolerance of 1m
		// printTrainArrDepMsg(Stations[0].stationName, "dep", trainIdx, t, "Input_EGTRAIN/Rescheduling");
		cout << "\n>>> " << trainDescription << " DEPARTED from " << Stations[0].stationName << " at t = " << t << " - " << simulationTime(t, initial_variables.startingSimulationTime) << endl;
	}
}

// print train service path diagram (append to file)
void Train::printTrainServicePathDiagram(std::string FolderName, int nextServiceRouteID) {
	std::string FileName = FolderName + "/TrainServicePathDiagram.txt";
	std::ofstream FileOutput;

	// avoid different corridor for initial service (entering station for the first time)
	if (prevIntendedDepTime == 0 && nextServiceRouteID != -1 && train_route[indexOfRoute].corridor != train_route[nextServiceRouteID].corridor && train_route[indexOfRoute].reversed_direction == train_route[nextServiceRouteID].reversed_direction) {
		indexOfRoute = nextServiceRouteID;
	}

	FileOutput.open((char*)FileName.c_str(), std::ios::binary | std::ios::app); // append

	FileOutput << trainDescription << "\t" << dispLineID << "\t" << train_route[indexOfRoute].reversed_direction << "\t" << train_route[indexOfRoute].corridor << "\t";

	const int activeFirst = earliestActiveTrajectoryIndex < 0
			? -1
			: std::max(earliestActiveTrajectoryIndex, prevIntendedDepTime);
	const auto exportCells = trajectoryExportCells(instant_spatial_position, activeFirst, End_Time);
	for (int t = 0; t < initial_variables.times; t++) {
		const double position = t < static_cast<int>(exportCells.size()) ? exportCells[t] : -9999;
		if (position != -9999) {
			// non-reversed route (same with/without jump)
			if (!train_route[indexOfRoute].reversed_direction) {
				FileOutput << position << "\t";
			}
			// reversed route without jump
			else if (train_route[indexOfRoute].diffRegionsJumpX.first == 0) {
				FileOutput << (train_route[indexOfRoute].OriginalRefReversedRoute - position) << "\t";
			}
			// reversed route with jump
			else {
				FileOutput << ((train_route[indexOfRoute].OriginalRefReversedRoute - position) - (train_route[indexOfRoute].diffRegionsJumpX.first * 1000)) << "\t";
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
		std::cout << "Error2 opening file to write arr/dep message\n";
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
	for (int i = 0; i < train_route[indexOfRoute].N_Block_Sections; i++) {
		if (((instant_spatial_position[t] - wagon * (train_length / total_nW)) >= train_route[indexOfRoute].sequence_of_block_sections[i].start_node.X * 1000) && (instant_spatial_position[t] - wagon * (train_length / total_nW)) < (train_route[indexOfRoute].sequence_of_block_sections[i].end_node.X * 1000)) {
			portion = (instant_spatial_position[t] - wagon * (train_length / total_nW)) - train_route[indexOfRoute].sequence_of_block_sections[i].start_node.X * 1000;

			// calculate X position
			if (train_route[indexOfRoute].reversed_direction) {
				X = (train_route[indexOfRoute].sequence_of_block_sections[i].GeoXBegNode - portion) / 1000;
			} else {
				X = (train_route[indexOfRoute].sequence_of_block_sections[i].GeoXBegNode + portion) / 1000;
			}
			return X;
		}
	}

	// did not find position correctly
	return -1;
}

// function to occupy entire single track
void Train::occupySingleTrack(Section* BS, int Blocks, int hTail, int hHead, int t) {
	// check all single tracks
	for (int l = 0; l < singleTrackLimits.size(); l++) {
		int firstBS = -1, lastBS = -1;
		bool outOfSingleTrack = true;
		std::string firstLimit = std::get<0>(singleTrackLimits[l]), secondLimit = std::get<1>(singleTrackLimits[l]);

		// reorder limits for reversed routes (limits always oriented with X increasing)
		if (train_route[indexOfRoute].reversed_direction) {
			firstLimit = std::get<1>(singleTrackLimits[l]);
			secondLimit = std::get<0>(singleTrackLimits[l]);
		}

		for (int i = 0; i < Blocks; i++) {
			// begin of single track
			if (BS[i].ID == firstLimit) {
				// train after begin single track
				if (hHead >= (i - 1)) {
					// block also previous signalling_block_sections
					firstBS = i - 1;

					// route bound
					if (firstBS < 0) {
						firstBS = 0;
					}
				}
				// train before single track
				else {
					break;
				}
			}

			// end of single track
			if (BS[i].ID == secondLimit) {
				// train before end single track
				if (hTail <= (i + 1)) {
					// occupy also next signalling_block_sections
					lastBS = i + 1;

					// route bound
					if (lastBS > (Blocks - 1)) {
						lastBS = Blocks - 1;
					}
				}
				// train after single track
				else {
					break;
				}
			}

			// position found when train route crosses the entire single track
			if (firstBS != -1 && lastBS != -1) {
				outOfSingleTrack = false;
				break;
			}
			// position found when train route crosses only one limit of single track
			else if ((i == (Blocks - 1)) && ((firstBS != -1) || (lastBS != -1))) {
				outOfSingleTrack = false;
			}
		}

		// single track occupied
		if (!outOfSingleTrack) {
			// set train occupying the single track
			std::get<2>(singleTrackLimits[l]) = type + std::to_string(ID);

			// find entrance/exit signalling_block_sections
			int prevBlocksST[2] = {-1, -1};
			int blocksST[2] = {-1, -1};
			for (int i = 0; i < ::Blocks; i++) {
				// prev signalling_block_sections entrance
				if (::signalling_block_sections[i].ID == std::get<0>(singleTrackLimits[l])) {
					prevBlocksST[0] = i;
				}
				// signalling_block_sections entrance
				if (::signalling_block_sections[i].ID == std::get<3>(singleTrackLimits[l])) {
					blocksST[0] = i;
				}
				// prev signalling_block_sections exit
				if (::signalling_block_sections[i].ID == std::get<1>(singleTrackLimits[l])) {
					prevBlocksST[1] = i;
				}
				// signalling_block_sections exit
				if (::signalling_block_sections[i].ID == std::get<4>(singleTrackLimits[l])) {
					blocksST[1] = i;
				}
			}

			// occupy entry and exit of single track
			for (int i = 0; i < 2; i++) {
				int h = blocksST[i];
				int Prev_Block = prevBlocksST[i];
				occupyBlockAndConnected(::signalling_block_sections[h], ::signalling_block_sections[Prev_Block], -1, -1);
				// if the Block Section has a switch in diverging position
				if (::signalling_block_sections[h].withSwitchDiv == true) {
					if ((::signalling_block_sections[h].SignallingLevel == 3) || (::signalling_block_sections[h].SignallingLevel == 4)) {
						activateBlocksWithSwitchesDiv(::signalling_block_sections[h], ::signalling_block_sections[Prev_Block].trackLineId, ::signalling_block_sections[h].XStartSwitch); // XStartSwitch to occupy all signalling_block_sections
					} else {
						activateBlocksWithSwitchesDivFixedBlock(::signalling_block_sections[h], ::signalling_block_sections[Prev_Block].trackLineId, -1);
					}
				}
			}
		}

		// release single track if train left it
		if (std::get<2>(singleTrackLimits[l]) == (type + std::to_string(ID))) {
			int hRelease = -1;

			// non-rev route (left end of single track - X-oriented)
			if (!train_route[indexOfRoute].reversed_direction && hTail > 1 && std::get<1>(singleTrackLimits[l]) == BS[hTail - 2].ID) {
				// train left single track 1 timestep ago
				if (((instant_spatial_position[(t - 1) - (int)(S_delay / timestep)] - train_length) < BS[hTail].end_node.X * 1000) && ((instant_spatial_position[(t - 1) - (int)(S_delay / timestep)] - train_length) >= BS[hTail].start_node.X * 1000)) {
					// release begin of single track
					// find signalling_block_sections to release
					for (int h = 0; h < ::Blocks; h++) {
						if (::signalling_block_sections[h].ID == std::get<3>(singleTrackLimits[l])) {
							hRelease = h;
							if (hRelease < 0) {
								hRelease = 0;
							}
							break;
						}
					}
				}
			}
			// rev route (left begin of single track - X-oriented)
			else if (train_route[indexOfRoute].reversed_direction && hTail > 1 && std::get<0>(singleTrackLimits[l]) == BS[hTail - 2].ID) {
				// train left single track 1 timestep ago
				if (((instant_spatial_position[(t - 1) - (int)(S_delay / timestep)] - train_length) < BS[hTail].end_node.X * 1000) && ((instant_spatial_position[(t - 1) - (int)(S_delay / timestep)] - train_length) >= BS[hTail].start_node.X * 1000)) {
					// release begin of single track
					// find signalling_block_sections to release
					for (int h = 0; h < ::Blocks; h++) {
						if (::signalling_block_sections[h].ID == std::get<4>(singleTrackLimits[l])) {
							hRelease = h;
							if (hRelease < 0) {
								hRelease = 0;
							}
							break;
						}
					}
				}
			}

			// release entrance of single track
			if (hRelease != -1) {
				releaseLastBlockAndConnected(::signalling_block_sections[hRelease]);

				// set single track as not occupied
				std::get<2>(singleTrackLimits[l]).clear();
			}
		}
	}
}

// unlock single track (unlock signalling_block_sections for a train passing a single track)
void Train::unlockSingleTrack(Section* BS, int Blocks, int t) {
	for (int l = 0; l < singleTrackLimits.size(); l++) {
		// single track occupied by this train
		if (std::get<2>(singleTrackLimits[l]) == (type + std::to_string(ID))) {
			// signalling_block_sections occupied by the train
			// collect all signalling_block_sections from head to tail
			int hTail = 0, hHead = 0;
			for (int h = 0; h < Blocks; h++) {
				if (((instant_spatial_position[t - (int)(S_delay / timestep)] - train_length) < BS[h].end_node.X * 1000) && ((instant_spatial_position[t - (int)(S_delay / timestep)] - train_length) >= BS[h].start_node.X * 1000)) {
					hTail = h;
				}
				if ((instant_spatial_position[t - (int)(S_delay / timestep)] < BS[h].end_node.X * 1000) && (instant_spatial_position[t - (int)(S_delay / timestep)] >= BS[h].start_node.X * 1000)) {
					hHead = h;
					break;
				}
			}

			// release end of single track (2 signalling_block_sections to avoid yellow and red signals)
			if (((hHead < (Blocks - 1)) && (!train_route[indexOfRoute].reversed_direction && BS[hHead + 1].ID == std::get<1>(singleTrackLimits[l])) || (train_route[indexOfRoute].reversed_direction && BS[hHead + 1].ID == std::get<0>(singleTrackLimits[l]))) || ((!train_route[indexOfRoute].reversed_direction && BS[hHead].ID == std::get<1>(singleTrackLimits[l])) || (train_route[indexOfRoute].reversed_direction && BS[hHead].ID == std::get<0>(singleTrackLimits[l])))) {
				int hRelease = hHead + 1;

				// changes on previous signalling_block_sections
				if (hRelease > 0) {
					snprintf(BS[hRelease - 1].state, sizeof(BS[hRelease - 1].state), "%s", "green");
					for (int k = 0; k < BS[hRelease - 1].total_arcs; k++) {
						BS[hRelease - 1].arcs_in_signalling_block_section[k].signalSpeedLimit = 999;
					}

					// revert changes to speeds
					double MinSpeedLim = BS[hRelease].arcs_in_signalling_block_section[0].speedLimit;
					if (BS[hRelease].arcs_in_signalling_block_section[0].signalSpeedLimit < MinSpeedLim) {
						MinSpeedLim = BS[hRelease].arcs_in_signalling_block_section[0].signalSpeedLimit;
					}

					// the speed limit at the end of block section BLS[h] is MinSpeedLim
					BS[hRelease - 1].arcs_in_signalling_block_section[BS[hRelease - 1].total_arcs - 1].speedInBraking = MinSpeedLim;
				}

				// changes on signalling_block_sections itself
				BS[hRelease].code = 270;
				BS[hRelease].exit_speed = 0;
			}
		}
	}
}

// function to protect all station areas
void protectStationAreas(int i) {
	for (int k = 0; k < numRegions; k++) {
		// cout << ">>>>>>>>>" << regional_train[k].trainDescription << "is OutOfSimulation" << regional_train[k].OutOfSimulation << endl;
		if (!regional_train[k].OutOfSimulation) {
			if ((i >= regional_train[k].departure_time) && regional_train[k].CanEnter) {
				// find signalling_block_sections occupied by head of train
				int hHead = 0; // hTail = 0;
				for (int h = 0; h < train_route[regional_train[k].indexOfRoute].N_Block_Sections; h++) {
					if ((regional_train[k].instant_spatial_position[i - (int)(S_delay / timestep)] < train_route[regional_train[k].indexOfRoute].sequence_of_block_sections[h].end_node.X * 1000) && (regional_train[k].instant_spatial_position[i - (int)(S_delay / timestep)] >= train_route[regional_train[k].indexOfRoute].sequence_of_block_sections[h].start_node.X * 1000)) {
						hHead = h;
						break;
					}
				}

				// occupy station interlocking area
				for (int s = 0; s < stationBoundarySections.size(); s++) {
					// check if section ahead of train is entrance of station
					if (stationBoundarySections[s].entrance->ID == train_route[regional_train[k].indexOfRoute].sequence_of_block_sections[hHead + 1].ID) {
						// check if plaform is booked or train leaving
						bool platformBooked = false;
						std::vector<std::string> stationsToProtect = {"Alm", "Asd", "Asdz"};
						if (std::find(stationsToProtect.begin(), stationsToProtect.end(), regional_train[k].Stations[regional_train[k].numStations - 1].stationName) != stationsToProtect.end()) {
							if (!stationBoundarySections[s].exit) {
								for (int tr = 0; tr < numRegions; tr++) {
									if (regional_train[tr].ID == regional_train[k].ID && regional_train[tr].type == regional_train[k].type) {
										continue;
									}
									if (regional_train[tr].Stations[regional_train[tr].numStations - 1].stationName == regional_train[k].Stations[regional_train[k].numStations - 1].stationName && regional_train[tr].reservedPlatform == regional_train[k].arrivalPlatform) {
										platformBooked = true;
										break;
									}
									// wait for trains leaving the station to prevent deadlocks
									if (regional_train[tr].Stations[0].stationName == regional_train[k].Stations[regional_train[k].numStations - 1].stationName) {
										if (train_route[regional_train[tr].indexOfRoute].reversed_direction != train_route[regional_train[k].indexOfRoute].reversed_direction) {
											// if the other train is not booking a platform, it is not at the platform - enough to check position
											if (train_route[regional_train[k].indexOfRoute].reversed_direction == false && regional_train[tr].trainXPosition(i) > regional_train[k].trainXPosition(i) && regional_train[tr].numStations > 1) {
												platformBooked = true;
												break;
											} else if (train_route[regional_train[k].indexOfRoute].reversed_direction == true && regional_train[tr].trainXPosition(i) < regional_train[k].trainXPosition(i) && regional_train[tr].numStations > 1) {
												platformBooked = true;
												break;
											}
										}
									}
								}
							}
						}

						stationBoundarySections[s].protectEntrance(hHead + 1, regional_train[k].indexOfRoute, platformBooked);
						break; // train occupies at most one station area
					}
					// check if section ahead of train is section before entrance of station
					else if (stationBoundarySections[s].entrance->ID == train_route[regional_train[k].indexOfRoute].sequence_of_block_sections[hHead + 2].ID) {
						// do not protect if section before entrance is occupied by another train, otherwise it will be blocked
						bool needToBlock = true;
						for (auto occ = BlocksOccupied.begin(); occ != BlocksOccupied.end(); ++occ) {
							if (train_route[regional_train[k].indexOfRoute].sequence_of_block_sections[hHead + 1].ID == *occ) {
								needToBlock = false;
								break;
							}
						}
						if (needToBlock) {
							// check if plaform is booked
							bool platformBooked = false;
							std::vector<std::string> stationsToProtect = {"Alm", "Asd", "Asdz"};
							if (std::find(stationsToProtect.begin(), stationsToProtect.end(), regional_train[k].Stations[regional_train[k].numStations - 1].stationName) != stationsToProtect.end()) {
								if (!stationBoundarySections[s].exit) {
									for (int tr = 0; tr < numRegions; tr++) {
										if (regional_train[tr].ID == regional_train[k].ID && regional_train[tr].type == regional_train[k].type) {
											continue;
										}
										if (regional_train[tr].Stations[regional_train[tr].numStations - 1].stationName == regional_train[k].Stations[regional_train[k].numStations - 1].stationName && regional_train[tr].reservedPlatform == regional_train[k].arrivalPlatform) {
											platformBooked = true;
											break;
										}
										// wait for trains leaving the station to prevent deadlocks
										if (regional_train[tr].Stations[0].stationName == regional_train[k].Stations[regional_train[k].numStations - 1].stationName) {
											if (train_route[regional_train[tr].indexOfRoute].reversed_direction != train_route[regional_train[k].indexOfRoute].reversed_direction) {
												// if the other train is not booking a platform, it is not at the platform - enough to check position
												if (train_route[regional_train[k].indexOfRoute].reversed_direction == false && regional_train[tr].trainXPosition(i) > regional_train[k].trainXPosition(i) && regional_train[tr].numStations > 1) {
													platformBooked = true;
													break;
												} else if (train_route[regional_train[k].indexOfRoute].reversed_direction == true && regional_train[tr].trainXPosition(i) < regional_train[k].trainXPosition(i) && regional_train[tr].numStations > 1) {
													platformBooked = true;
													break;
												}
											}
										}
									}
								}
							}

							stationBoundarySections[s].protectEntrance(hHead + 2, regional_train[k].indexOfRoute, platformBooked);
						}
						break; // train occupies at most one station area
					}
					// check if train entered station area
					else if (stationBoundarySections[s].entrance->ID == train_route[regional_train[k].indexOfRoute].sequence_of_block_sections[hHead].ID) {
						// FOR NOW, IGNORE AREAS WITH EXIT SECTION
						if (stationBoundarySections[s].exit) {
							continue;
						}

						regional_train[k].reservedPlatform = regional_train[k].arrivalPlatform;
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
	instant_block_section_occupied = std::vector<std::string>(vec_size);
	instant_train_energy_consumption = std::vector<double>(vec_size, 0);
	BX = std::vector<double>(vec_size, 0);
	Xob = std::vector<double>(vec_size, 0);
	Vob = std::vector<double>(vec_size, 0);
	Eq = std::vector<int>(vec_size, 0);

	// previously on train class constructor (there won't work because size of vector is zero until user sets length of simulation and constructor is called before that)
	instant_train_energy_consumption[0] = 0;
}
