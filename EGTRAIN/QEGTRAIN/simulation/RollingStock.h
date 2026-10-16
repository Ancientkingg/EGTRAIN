#ifndef Rolling_Stock_hpp
#define Rolling_Stock_hpp
#ifdef signals
#define EGTRAIN_RESTORE_SIGNALS_KEYWORD
#undef signals
#endif
#include "scene/SceneModel.h"
#ifdef EGTRAIN_RESTORE_SIGNALS_KEYWORD
#define signals Q_SIGNALS
#undef EGTRAIN_RESTORE_SIGNALS_KEYWORD
#endif
#include "simulation/Signalling.h"
#include "simulation/RuntimeLimits.h"
#include "util/Log.h"
#include "util/TrajectoryUtil.h"

// GUI - Virtual Coupling notifications
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <limits>
#include <vector>
extern vector<int> VCmsgTimestep;
extern vector<string> VCmsgTrain;
extern vector<string> VCmsgText;

// -------------


#include <cmath> // std::fabs

extern int N_OrderLists; // This is the number of OrderLists that have to be respected at critical nodes
extern InitialParameters initial_variables;

// Build rolling stock, timetable, selected-scenario incidents, entrance
// delays, platforms, and passengers from an in-memory canonical scene.
// The function is transactional: errors leave existing operation globals intact.
std::vector<SceneDiagnostic> buildOperationsFromScene(const SceneModel& scene,
	const std::string& selectedScenarioId = {},
	const SceneRunSelection& selectedOccurrences = {});

// Clear operation metadata before preparing a scene without rebuilding train slots.
void prepareNativeOperationsState();

// Release native train-owned arrays and operation globals between scene runs.
void resetNativeOperationsState();

// Passenger capacity of a train: 300 for the traction unit and 300 for each wagon.
int trainPassengerCapacity(double numberOfWagons);

// Occupancy as a ratio of passengers to capacity. An unknown capacity (zero or
// negative) gives 0, so it never counts as congested.
double passengerOccupancyRatio(int passengers, int capacity);

class OrderList {
public:
	string ID;
	string BlockID;								   // This is the ID of the Block Section where the ordere need to be implemented
	double Node_X;								   // This is the abscissa of the Node where the departure order has to be respected
	bool Is_MergingJunction, Is_DivergingJunction; // These 2 variables indicate whether the OrderList is a Diverging or a Merging Junction. By default they are set to 0;
	TrainEvent TE[300];
	int numTeList;
	string LastEnteredTrain; // This is the trainDescription of the Last train that has departed from the Node Node_X
	list<string> Implemented_Order;
	OrderList();

	// Function to detect the implemented train order at the OL
	void Detect_Implemented_Order() {
		if (Implemented_Order.empty() || (LastEnteredTrain != Implemented_Order.back())) { // if the list is empty or the LastEnteredTrain is different from the last Element of the list
			if (LastEnteredTrain != "None")												   // And LastEnteredTrain is also different from None
				Implemented_Order.push_back(LastEnteredTrain);							   // add the train to the list
		}
	}

	// Function to Print the Implemented Order in a text file
	// Print this ordered list on a text file
	void Print_Implemented_Order(string FolderName, int Index_OL);
};

extern OrderList OL[20];


class BlockingTimes {
public:
	string BlockID;
	string NextBlockID; // This is the ID of the location or block section after this blocking time. By default the location after this one is a VirtualTDSB the location after the VirtualTDSB will be considered. if the location after VirtualTDSB is the last infra element of the route then NextBlockID is None. NextBlockID by default is never a VirtualTDSB
	string trainDescription;
	double StartRunTime;
	double EndRunTime;
	double StartApproachTime;
	double EndApproachTime;
	double StartClearTime;
	double EndClearTime;
	double length;
	double StartOccTime;
	double EndOccTime;
	double setupTime;
	double sightReacTime;
	double ApproachTime;
	double RunTime;
	double clearingTime;
	double ReleaseTime;
	double RunTimeMargin;
	double PosStart;
	double PosEnd;
	double GeoPosStart;
	double GeoPosEnd;
	string ConnectedBlockingTimeID;				 // This is the ID of another infrastructure element which is connected to the current one. For example if this is the starting edge of a switch its blocking time will be connected to the one of the ending edge of that switch
	double PosConnectedBlockingTime;			 // This is the Position of the connected Blocking Time along the route of the train
	double SpeedPreviousTrain;					 // This variable represents the speed of the train which previously crossed the infrastructure element. This variable is needed in the case of Virtual Coupling to compute the approaching time (that in such a case is the time needed by the train to accelerate/decelerate from its current speed to the speed of the previous train)
	string NamePreviousTrain;					 // This is the name of the previous train that has crossed a given infrastructure element
	string NextBlockIDPreviousTrain;			 // This is the ID of the next block section or location for the previous train
	int SignallingLevel;						 // This is the signalling level of the block section
	bool IsEndOfDivSwitchBeingStartOfADivSwitch; // Again this is the same variable appearing in the infrastructure elements. This bool variable is true when the infrastructure element we are computing the blocking time for is the ending edge of a diverging switch coninciding with the starting edge of another diverging switch
	bool InfraElementInPositionForTrain;		 // This is to indicate whether the Switch is in the right position for the train to cross, if it already in the right position then no additional setuptime needs to be added, otherwise additional setup time needs to be added
	bool LocationWithSwitch;
	bool IsAlreadyUniformedToConnectedSwitch; // This variable when it is true means that the blocking time has been already uniformed with the Blocking Time of the connected edge of diverging switch
	string SwitchName;						  // This is to indicate whether the location or block section has a switch or not
	string stationName;
	bool IsComplete;

	BlockingTimes();

	BlockingTimes& operator=(const BlockingTimes& obj2);

	// Check that the last recorded status of the infrastructure element is in line with what the train needs
	void checkIfLastInfraElementStatusIsInLineWithTrainNeeds(string TrainName, list<InfraEvent> ListAllInfraEvents);
};

// Blocking times of one train, indexed like an array. Indexing a non-const list
// past the end grows it with default-constructed entries; growing never moves
// existing entries. Indexing a const list never grows it.
class BlockingTimeList {
public:
	BlockingTimes& operator[](int index) {
		checkIndex(index);
		if (index >= static_cast<int>(entries.size()))
			entries.resize(static_cast<std::size_t>(index) + 1);
		return entries[static_cast<std::size_t>(index)];
	}

	const BlockingTimes& operator[](int index) const {
		checkIndex(index);
		static const BlockingTimes unset;
		if (index >= static_cast<int>(entries.size()))
			return unset;
		return entries[static_cast<std::size_t>(index)];
	}

private:
	static void checkIndex(int index) {
		if (index < 0) {
			std::fprintf(stderr, "BlockTime index %d is negative\n", index);
			std::abort();
		}
	}

	std::deque<BlockingTimes> entries;
};

/**************************************************************************************************************************
*
Declaration of Train class
*
****************************************************************************************************************************/
extern int numRegions; /*Number of Speed ranges in the characteristic Tractive effort-speed curve of trains, Number of Trains loaded for the simulation( N_Train+N_TrainD)*/

extern int N_Train, N_TrainD; /*Number of Trains with even path, Number of Trains with odd path*/

// Largest number of trains a scene may expand to. It is a validation and editor
// limit, not a storage size: regional_train holds exactly the trains of the scene.
inline constexpr int Max_N_Reg = RuntimeLimits::kMaxExpandedTrains;

// A train that has braked into a stop is parked kStopHoldbackM short of the stopping point; the
// parking assignments below write the same 0.0001 as a literal. The parked position and a
// stopping point that is compared with it are rounded differently on targets that fuse multiply
// and add, so they can differ by a few units in the last place in either direction. The tolerance
// is far above that error and far below any real distance. A position within the tolerance of the
// hold-back position is at the stop: it is neither short of it nor past it.
inline constexpr double kStopHoldbackM = 0.0001;
inline constexpr double kStopHoldbackToleranceM = 1e-6;
inline bool isShortOfBrakingPoint(double position, double brakingPoint) {
	return position < brakingPoint - kStopHoldbackM - kStopHoldbackToleranceM;
}
inline bool isPastStopHoldback(double position, double stoppingPoint) {
	return position > stoppingPoint - kStopHoldbackM + kStopHoldbackToleranceM;
}

class Train {
public:
	double number_of_wagons = 0.0; /*!< number of Wagons*/
	double massFactor = 0.0;	   // mass factor
	double mass_of_traction_unit = 0.0, mass_of_a_wagon = 0.0, total_train_mass = 0.0, max_train_decelaration = 0.0, max_train_speed = 0.0, g = 0.0, frontal_wagon_area = 0.0, massPerWagonAxle = 0.0, Jerk = 0.0, train_length = 0.0;
	// Mass of the Traction Unit(Kg), Mass of a Wagon(Kg), Total Mass(Kg), Maximum Train Deceleration(m/s2), Maximum Train Speed(m/s),Gravity Acceleration, Frontal Wagon Area , Jerk(m/s3), Length (m)
	std::vector<double> instant_train_speed; // Istantaneous Train Speed Vector(m/s)
	int velocityIntervals;					 // Number of intervals of speed for the Traction Unit of the Train
	double Vlb[20] = {}, Vub[20] = {};		 // Traction Unit Characteristic Speeds (Lower Bound Speed, Upper Bound Speed)
	double C0[20] = {}, C1[20] = {}, C2[20] = {};
	double resistanceCoefficient = 0.0;			  // Traction Curve Coefficients, Resistance coefficients
	std::vector<double> instant_spatial_position; // Vector of istantaneous Spatial position of the Train
	std::vector<std::string> instant_block_section_occupied;
	std::vector<double> instant_train_power_consumption;  // Vector of istantaneous Train Power Consumption [W]
	std::vector<double> instant_train_tractive_effort;	  // Branch-applied wheel effort [N], aligned with trajectory samples
	std::vector<double> instant_train_energy_consumption; // Vector of istantaneous Train Energy Consumption [MJ]
	double TotalEnergyConsumed, TotalEnergySubstationRequest, TotalEnergyConsWithRegBrak, TotalEnergySubstRequestWithRegBrak;
	// These values represent the total energy consumed by the train with no regenerative braking, the total energy requested to the SubStation, and the TotalEnergy consumed with regenerative braking;
	double EnergyForAuxiliaries = 0.0;	   // this is the amount of energy needed by auxiliaries and it is assumed as the 10% of the total energy consumption
	double departure_time = 0.0;		   // departure_time
	int brakingPoint;					   // brakingPoint is the position of the braking step after the intersection point between acceleration and braking curve
	double scheduled_departure_time = 0.0; // This is the departure time in the initial timetable not the actual scheduled_departure_time
	int temp, stop, counter;			   // Temporary variables for simulating train movement(temp=instant of time in which train starts braking, stop=time instant in which train starts stopping at a station), counter: number of times in which it enters in braking condition equation
	double ID = 0.0;					   // Train ID number
	string type;						   // This specifies if it is an Intercity, a regional or a metro train
	string operatingCode;
	string serviceId;
	int serviceOccurrence = 1;
	double servicePerformancePercent = 100.0;
	bool hasConfiguredMaximumSpeed = false;
	double configuredMaximumSpeedKmh = 0.0;
	double compositionMaximumSpeedMs = 0.0;
	double appliedMaximumSpeedMs = 0.0;
	double appliedMaximumSpeedKmh = 0.0;
	std::vector<std::string> directIncidentIds;
	double firstDirectIncidentTime = -1.0;
	double firstDirectIncidentLocation = -1.0;
	bool destinationTerminationRequested = false;
	bool destinationTerminated = false;
	Arc As;					   // Temporary Arc object representing the Arc occupied by the train in a determined time instant
	Section Bs;				   // Temporary Block Section Object representing the Block Section occupied by the Train in a certain time instant
	double Start_Node_X = 0.0; // It represents the X of the Node from which the train starts its run
	string TrainRouteID;	   // This is the ID of the Route that the train has to follow
	int indexOfRoute;		   // This is the Index (ranging from 0 and N_Routes-1) of the Route with ID=TrainRouteID
	string trainDescription;   // This is the Description of the train which is the union between the Type and the ID: trainDescription = Type+ID
	bool direction;
	bool GradientExceptionInBraking; // This variable is true when because of a too steep gradient the braking curve computed by the function DrawBrakingCurve has a point having a speed lower than the final speed V2 (in this case the train will first decrease its speed to this value lower than V2 and then reaccelerate to V2)
	std::vector<double> BX;
	std::vector<Node> Stations;														// Stop nodes of the train in service order
	int numStations;																// Number of stops in use; equals Stations.size() for a train built from a scene
	static constexpr int kMaxTimetableStations = RuntimeLimits::kMaxTimetableStops; // Capacity of the station-indexed arrays below; stations beyond this cap have no timetable slot
	static int clampStationCount(int requested, const string& trainId);				// Clamps a served-station count to kMaxTimetableStations, warning once per train
	int stationBlockSection[kMaxTimetableStations] = {};							// Cached block section index for each station (avoids full-route scan every timestep)
	int stationArc[kMaxTimetableStations] = {};										// Cached Arc index within block section for each station
	bool StoppedForServiceStop = false;												// This variable is true when the train is stopping at a station to perform a service stop
	string CurrentServiceStop;														// This variable indicates the name of the Station the train is currently stopping at when StoppedForServiceStop=true
	string CurrentServiceStopPlatform;												// Id of the platform at which the train has stopped when making a service stop at a scheduled station/stop>

	double StationArrivals[kMaxTimetableStations];		  // This variable is the time instant in which the train actually enters a station
	string StationArrivalNames[kMaxTimetableStations];	  // Preserves served station names for post-run arrival analysis.
	double StationDelay[kMaxTimetableStations];			  // This variable represent the arrival delay of a train at a certain station
	double StationConsecDelay[kMaxTimetableStations];	  // This is the Consecutive delay at stations i.e. ArrivalDelay-Entrance Delay-Sum of disturbances at previous stations
	double StationDisturbance[kMaxTimetableStations];	  //  This variable represents the disturbance to dwell times at stations
	int N_Station_Stopped;								  // This variable counts the number of stations at which the train has stopped
	double ScheduledArrivals[kMaxTimetableStations] = {}; // This Array contains scheduled train arrival time at stations (it is necessary to calculate train deviations from timetable and therefore train delays)
	double ScheduledDepartures[kMaxTimetableStations] = {};
	int delayed;						// This is a boolean parameter: if delayed =1 the train will experience a delay at a determined station, else if delayed=0 the train won't experience any delay
	double TotalInputDelays = 0.0;		// Sum of the Entrance Delays +stochastic disturbances to dwell times
	double EntranceDelay;				// This is the entrance delay of the train on the network
	bool CanEnter;						// This Variable becomes True when time is major than departure_time and if for the first Time signalling_block_sections[0] is free (i.e. its code is 270)
	bool OutOfSimulation;				// if true it means that the train is out of the simulation, i.e. it has been already simulated
	int counter2;						// When Counter2 becomes 1, CanEnter variable becomes true and the train can start the simulation
	bool IsTrainCoupling;				// This int variable counts the number of times that the train receives a MA in Signalling Level 4 where it could Virtually Couple to another train ahead having the same direction of movement and the a shared route ahead
	bool IsTrainInFollowingMode;		// This boolean is true when the train is following another one in VC mode
	bool IsInUnintentionalDecoupling;	// This variable is set to true when a train in following mode starts outdistancing from the leader because of conditions due to speed limitations, gradient or curvature radii on the tracks is crossing and not to an intentional decoupling due to a MA where the route diverges from the leader's
	bool IsTrainDecoupling;				// This boolean is true when the train is Decoupling from the VC mode
	int CounterFollowingMode;			// This variable counts the seconds the train is in following mode under Virtual Coupling
	string LeadingTrainInFollowingMode; // This is the trainDescription of the leading train when this train is in following mode
	double Xobmin, Vobmin;
	std::vector<double> Xob, Vob;
	std::vector<double> Sbrak = std::vector<double>(2000, 0.0), Vbrak = std::vector<double>(2000, 0.0); // Vector Sbrak: Vector of curviline abscissas belonging to a braking curve;  Vector Vbrak: Vector of speeds belonging to a braking curve
	int BrakStep;
	bool BrakingForEoA = false;					  // When this is true it means that the train is braking because of an ETCS End of Authority on the route
	MovementAuthority Predicted_MA_To_CoupleAt;	  // This is the Movement Authority where the train is predicted to virtually couple to a train ahead
	MovementAuthority Last_Received_MA;			  // This is the last movement authority applied to the train
	MovementAuthority Last_MA_StoppedAt;		  // this is the last movement authority the train has stopped at
	MovementAuthority Predicted_MA_To_DecoupleAt; // This is the movement authority where the train is predicted to decouple from the train ahead
	std::vector<int> Eq;						  // Number of speed interval of theTraction curve function
	int RunStartTime;							  // Represents the time instant in which the train start his run from the beginning (In normal conditions it's equal to Train headway, but when the first light is red, this variable assumes a value higher than Headway)
	BlockingTimeList BlockTime;					  // This represents the blocking time of the train
	int N_BlockSections;						  // This is the total Number of BlockSections and therefore Blocking Times
	int N_BlockTimeComplete;
	int numOverlaps; // This is the number of train overlaps
	double Final_Delay;
	int End_Time;							// Time instant in which the train exits simulation
	int earliestActiveTrajectoryIndex = -1; // First simulation sample in the active run
	list<Location> LocationNames;			// This LocationNames contain the name of block sections, or stations crossed by the train along its route
	list<string> ConflictingTrains;			// These are the trains which have common block sections.
	int N_ConflictingTrains;				// Number Of Conflicting Trains
	double ETCS3StoppingPoint;				// This is the point in km where the train is stopped because of ETCS 3 Movement Authority
	bool IsTrainStoppedForEoA;				// This boolean is true when the train is stopped because it reached the ETCS 3 End of Authority
	list<TrainEvent> TimetablePoints;		// This lists contains the arrival and departure of the train at each interlocking area of the network either those where the train stops and those where they train does not stop at
	int prevIntendedDepTime;				//  stores the previous intended dep time from dispatching tool
	int dispLineID = -1;					//  lineID from dispatching tool
	int arrivalPlatform = -1;				//  arrival platform from dispatching tool
	int reservedPlatform;					//  variable to book platform at destination
	// std::map<char, std::list<std::pair<int, double>>> trainCorrPosTime; // train position over time for each corridor

	float GibsonDwellTimeParameters[8]; // this is a vector of floats to store the Gibson dwell time parameters of the trains
	int MAX_OnBoard_Passengers, Current_OnBoard_Passengers;

	// Class Default Values

	Train();
	// Pre-compute the (block section, Arc) index for each station Node.
	// Called after Stations or route changes; eliminates the O(N_BS*total_arcs*numStations)
	// triple scan from the per-timestep hot path.
	void cacheStationPositions();


	// Calculation of Tractive Effort of the Traction Unit

	virtual double tractiveEffort(double V) {
		double Ftr = 0.0;
		for (int i = 0; i < velocityIntervals; i++) {
			if ((V >= Vlb[i]) && (V < Vub[i])) {
				Ftr = C0[i] + C1[i] * V + C2[i] * pow(V, 2);
			}
		}
		if (servicePerformancePercent == 100.0)
			return Ftr;
		return Ftr * servicePerformancePercent / 100.0;
	}

	//  Calculation of Braking Effort affected by Jerk

	virtual double brakingEffort(double T) {
		double FBr;
		if (Jerk * T < max_train_decelaration) {
			FBr = total_train_mass * massFactor * Jerk * T;
		} else {
			FBr = total_train_mass * massFactor * max_train_decelaration;
		}
		return FBr;
	}


	//! Calculation of Traction Unit Resistances due to air viscosity
	//!  (Modified Strahl Formulae_Italian FS Formulation)
	virtual double traction_unit_resistences(double V) {
		double FRt;
		FRt = 4.2 * (mass_of_traction_unit * g / 1000) + 0.72 * pow(((V + 4.17) * 3.6), 2);
		return FRt;
	}

	//! Calculation of Train Wagons Resistances using the Sauthoff Formula
	virtual double wagons_resistances(double V) {
		double Frwp;
		Frwp = (1.9 + resistanceCoefficient * (V * 3.6)) * (massPerWagonAxle * g / 1000) + 4.8 * (number_of_wagons + 2.7) * frontal_wagon_area * pow(((V + 4.17) * 3.6 / 10), 2); // Sauthoff Formulae for Passenger Trains
		return Frwp;
	}

	//! Calculation of Train Resistances due to Track Gradient
	virtual double gradient_resistances(double G) {
		double Frlg;
		Frlg = g * (total_train_mass)*G;
		return Frlg;
	}

	//! Calculation of Train Resistances due to Track Curvature
	virtual double curvature_resistances(double R) {
		if (R == 0.0)
			return 0.0;
		double Frlc;
		Frlc = g * (total_train_mass) * 700 / (R * 1000);
		return Frlc;
	}

	//! Calculation of the sum of all Train Resistances
	virtual double total_train_resistances(double V, double G, double R) {
		double FR;
		FR = traction_unit_resistences(V) + wagons_resistances(V) + gradient_resistances(G) + curvature_resistances(R);
		return FR;
	}

	virtual void Initialise_Gibson_Dwell_Time_Parameters(float beta0, float beta1, float beta2, float beta3, float beta4, float beta5, float beta6, float beta7) {
		this->GibsonDwellTimeParameters[0] = beta0;
		this->GibsonDwellTimeParameters[1] = beta1;
		this->GibsonDwellTimeParameters[2] = beta2;
		this->GibsonDwellTimeParameters[3] = beta3;
		this->GibsonDwellTimeParameters[4] = beta4;
		this->GibsonDwellTimeParameters[5] = beta5;
		this->GibsonDwellTimeParameters[6] = beta6;
		this->GibsonDwellTimeParameters[7] = beta7;
	}

	//! Calculation of Train Energy Consumption
	double train_energy_consumption(int t) {
		double Pot1 = 0;
		double Pot2 = 0;
		if (instant_train_power_consumption[t - 1] >= 0)
			Pot1 = instant_train_power_consumption[t - 1];
		else
			Pot1 = 0;
		if (instant_train_power_consumption[t] >= 0)
			Pot2 = instant_train_power_consumption[t];
		else
			Pot2 = 0;
		instant_train_energy_consumption[t] = instant_train_energy_consumption[t - 1] + ((Pot1 + Pot2) / 2000000) * timestep;
		return instant_train_energy_consumption[t];
	}

	// A completed trajectory can leave a non-finite power sample at End_Time;
	// treat that boundary sample and its aligned effort as zero, matching
	// train_energy_consumption().
	void sanitizeTerminalPowerSample() {
		if (End_Time >= 0 && End_Time < static_cast<int>(instant_train_power_consumption.size())
			&& !std::isfinite(instant_train_power_consumption[static_cast<std::size_t>(End_Time)])) {
			instant_train_power_consumption[static_cast<std::size_t>(End_Time)] = 0.0;
			if (End_Time < static_cast<int>(instant_train_tractive_effort.size()))
				instant_train_tractive_effort[static_cast<std::size_t>(End_Time)] = 0.0;
		}
	}

	// Function to calculate Total Energy Consumed with and without regenrative braking (the inputs are the Eta of the Substation and the Eta of the regenerative braking)
	void TotalEnergyConsumptionWithAndWithoutRegBraking(double EtaSubst, double EtaRegBrak) {
		sanitizeTerminalPowerSample();
		// This is the total Energy consumed by the train to move
		this->TotalEnergyConsumed = instant_train_energy_consumption[End_Time];
		this->EnergyForAuxiliaries = 0.10 * TotalEnergyConsumed;

		// Adding the EnergyForAuxiliaries to the total Energy consumed
		TotalEnergyConsumed = TotalEnergyConsumed + EnergyForAuxiliaries;

		// The total Energy requested to the Substation is TotalEnergyConsumed/EtaSubst
		this->TotalEnergySubstationRequest = TotalEnergyConsumed / EtaSubst;

		// Computing the EnergyConsumedwith the regenerative braking

		int TrainStartTime = (int)departure_time;
		TotalEnergyConsWithRegBrak = instant_train_energy_consumption[TrainStartTime]; // Initialize the first value of the totalEnergyConsumption

		for (int i = TrainStartTime + 1; i <= End_Time; i++) {

			double Pot1 = 0, Pot2 = 0;
			// Setting Pot1
			if (instant_train_power_consumption[i - 1] >= 0)
				Pot1 = instant_train_power_consumption[i - 1];
			else
				Pot1 = EtaRegBrak * instant_train_power_consumption[i - 1];
			// Setting Pot 2
			if (instant_train_power_consumption[i] >= 0)
				Pot2 = instant_train_power_consumption[i];
			else
				Pot2 = EtaRegBrak * instant_train_power_consumption[i];

			this->TotalEnergyConsWithRegBrak = TotalEnergyConsWithRegBrak + ((Pot1 + Pot2) / 2000000) * timestep;
		}
		// Adding the energy for auxiliaries to the TotalEnrgy Consuption with regenerative braking
		TotalEnergyConsWithRegBrak = TotalEnergyConsWithRegBrak + EnergyForAuxiliaries;

		this->TotalEnergySubstRequestWithRegBrak = TotalEnergyConsWithRegBrak / EtaSubst; // this is the energy requested rom the substation by the train when it uses regenerative braking
	}

	// Check the entrance possibility: This Function checks if the train can Enter the simulation (i.e. when time>=departure_time and for the first time signalling_block_sections[0] is free)
	/*void checkEntrance(int i,Section *signalling_block_sections){ if((ID==1)||(ID==111)||(ID==113)||(ID==N_Train+1)){
	if((i>=departure_time)&&(signalling_block_sections[0].code==270)){counter2++;
	if(counter2==1){CanEnter=true;}}}
	else{
	//Condition for Even Train Runs (i.e. Runs with direction =0)
	if(direction==0){
	if((i>=departure_time)&&(VirtualQ[(int)ID-2]==ID-1)&&((signalling_block_sections[0].code==180)||(signalling_block_sections[0].code==270))){counter2++;
	if(counter2==1){CanEnter=true;}}}
	//Condition for Odd Train Runs (i.e. Runs with direction =1)
	else{
	if((i>=departure_time)&&(VirtualQD[(int)ID-N_Train-2]==ID-1)&&((signalling_block_sections[0].code==180)||(signalling_block_sections[0].code==270))){counter2++;
	if(counter2==1){CanEnter=true;}}}

	}
	}*/

	//! Check the entrance possibility:
	//! This Function checks if the train can Enter the simulation
	//!  (i.e. when time>=departure_time and for the first time signalling_block_sections[0] is free)
	bool entryConflictsWithCrossover(int step, const Section& origin) const;

	void checkEntrance(int i, Section* BS) {
		// Where a signalling level applies, one train enters a section per step: a train that entered earlier in
		// this step has put the section in BlocksOccupied, which the aspect only shows from the next step. The list
		// is cleared at the end of every step, so here it holds only entries of this step. Waiting trains are
		// visited in the order in which they are due, so the one due first enters first.
		const bool waiting = !CanEnter;
		const bool separated = BS[0].SignallingLevel >= 0;
		if (waiting && separated && std::find(BlocksOccupied.begin(), BlocksOccupied.end(), BS[0].ID) != BlocksOccupied.end())
			return;
		if (waiting && isFirstSectionFailed(BS[0])) // enters when the failure has ended, at any signalling level
			return;
		if (waiting && separated && i >= departure_time && entryConflictsWithCrossover(i, BS[0]))
			return;
		string PreviousTrain = "None"; // the previous Train according to the list
		int IndexOL = BS[0].arcs_in_signalling_block_section[0].startNode.indexOrderList;

		if ((IndexOL >= 0) && (IndexOL < N_OrderLists) && (IndexOL < 20) && (OL[IndexOL].numTeList > 0)) {
			EG_LOG(SimStep, Info) << "this is the indexOL TRUE \n"
								  << IndexOL;
			for (int j = 1; j < OL[IndexOL].numTeList; j++) { // Determining which is the previous train in the list
				if (trainDescription == OL[IndexOL].TE[j].trainDescription)
					PreviousTrain = OL[IndexOL].TE[j - 1].trainDescription;
			}
			// if the Previous Train has already departed or if this train is the first of the list to depart
			if ((PreviousTrain == OL[IndexOL].LastEnteredTrain) || (PreviousTrain == "None") || (trainDescription == OL[IndexOL].TE[0].trainDescription)) {
				// if the time is equal or higher than the departure time and the signals are permissive set CanEnter true
				if ((BS[0].SignallingLevel == 3) || (BS[0].SignallingLevel == 4)) {
					bool IsRouteStartingPointOccupied = false; // this bool is true if start_node of signalling_block_sections[0] is present in the list of Movement Authorities ETCS_MA, i.e. if the beginning of the train route is occupied by another train
					if (ETCS_MA.size() > 0) {
						for (list<MovementAuthority>::iterator it = ETCS_MA.begin(); it != ETCS_MA.end(); it++) {
							// if the list of Movement Autority includes a Movement Autority related to the beginning Node of the Route which belongs to a train different from this, then break the for loop over the list of ETCS_MA and set the boolean IsRouteStartingPointOccupied to true
							if ((it->BSID == BS[0].ID) && (abs(it->AbsPosEoA - BS[0].GeoXBegNode) < 0.001) && (it->TrainInfo.trainDescription != this->trainDescription) && obeysAuthority(*it)) {
								IsRouteStartingPointOccupied = true;
								break;
							}
						}
					}
					if ((i >= departure_time) && (IsRouteStartingPointOccupied == 0)) {
						counter2++;
						if (counter2 == 1)
							CanEnter = true;
					}
				} else {
					if ((i >= departure_time) && ((BS[0].code == 270) || (BS[0].code == 180))) {
						counter2++;
						if (counter2 == 1)
							CanEnter = true;
					}
				}
			}
		}

		else { // In case there is no ordered sequence of trains to be respected by trains at the beginning of the route

			if ((BS[0].SignallingLevel == 3) || (BS[0].SignallingLevel == 4)) { // if the signalling system of the firt block section is ETCS Level 3 or Level 4 (Virtual Coupling)
				bool IsRouteStartingPointOccupied = false;						// this bool is true if start_node of signalling_block_sections[0] is present in the list of Movement Authorities ETCS_MA, i.e. if the beginning of the train route is occupied by another train
				if (ETCS_MA.size() > 0) {
					for (list<MovementAuthority>::iterator it = ETCS_MA.begin(); it != ETCS_MA.end(); it++) {
						// if the list of Movement Autority includes a Movement Autority related to the beginning Node of the Route which belongs to a train different from this, then break the for loop over the list of ETCS_MA and set the boolean IsRouteStartingPointOccupied to true
						if ((it->BSID == BS[0].ID) && (abs(it->AbsPosEoA - BS[0].GeoXBegNode) < 0.001) && (it->TrainInfo.trainDescription != this->trainDescription) && obeysAuthority(*it)) {
							IsRouteStartingPointOccupied = true;
							break;
						}
					}
				}
				if ((i >= departure_time) && (IsRouteStartingPointOccupied == 0)) {
					counter2++;
					if (counter2 == 1)
						CanEnter = true;
				}

			} else { // if instead the signalling system of the first block section is a fixed-block signalling system
				if ((i >= departure_time) && ((BS[0].code == 270) || (BS[0].code == 180))) {
					counter2++;
					if (counter2 == 1)
						CanEnter = true;
				}
			}
		}
		if (waiting && separated && CanEnter) // the entering train occupies the first section at once
			occupyBlockAndConnected(BS[0], BS[0], Start_Node_X * 1000, Start_Node_X * 1000);
	}

	// Function to compute dwell times based on the interaction with passengers
	// By default the dwell time is computed based on the microscopic dwell time model by Fernandez et al. (2007) which extends the model by Gibson et al. (1989)
	double computePaxDependentDwellTimeAtStations(int N_BoardPax, int N_AlightPax, double PlatformOccupancyRate, float beta0, float beta1, float beta2, float beta3, float beta4, float beta5, float beta6, float beta7);

	// Function to detect if a train has to stop at a Node to respect a certain order :
	/**this function returns true if the train has to stop, false otherwise*/

	bool Train_Must_Stop_For_OL_Order(bool respectOrder, int IndexOrderList);

	// This Function tests if the conditions for the train to stop at a certain point subsist:
	/**the train has to stop at a Node with Respsect Order=1 when
   the Previous train has not passed yet and when the aspect of its block section is restrictive(red or red_red)*/
	bool Conditions_To_Stop_Train_For_OL_Order(bool respectOrder, int IndexOrderList, const Section& BS);

	// Function to Update the LastEntered Train for the all OrderLists OL in the network
	void Update_LastEnteredTrain_For_All_OL_Improved(int i, const Section& BS);

	// A signal failure makes an End of Authority for every route copy of the failed section, and a held single-track
	// section makes one of the same type for the routes against its holder. Its position is expressed for the
	// direction of its route, so only trains of that direction obey it.
	bool obeysAuthority(const MovementAuthority& authority) const {
		return authority.type != "SignalFailure" || authority.ReversedDirection == train_route[indexOfRoute].reversed_direction;
	}

	// True while a signal failure of the first section of the route holds this train at its entry
	bool isFirstSectionFailed(const Section& firstSection) const {
		for (const MovementAuthority& authority : ETCS_MA)
			if (authority.type == "SignalFailure" && authority.BSID == firstSection.ID && obeysAuthority(authority)
				&& abs(authority.AbsPosEoA - firstSection.GeoXBegNode) < 0.001)
				return true;
		return false;
	}

	double authorityRoutePosition(const MovementAuthority& authority, const Section& section) const {
		return train_route[indexOfRoute].reversed_direction
			? section.GeoXBegNode - authority.AbsPosEoA + section.start_node.X * 1000
			: authority.AbsPosEoA;
	}

	// Resolve only the selected, currently published forward Tale within the EVC's existing scope.
	const MovementAuthority* currentFollowingAuthority(double head, double& leaderHead) const {
		const Route& route = train_route[indexOfRoute];
		int current = -1;
		for (int block = 0; block < route.N_Block_Sections; ++block)
			if (head >= route.sequence_of_block_sections[block].start_node.X * 1000
				&& head < route.sequence_of_block_sections[block].end_node.X * 1000) {
				current = block;
				break;
			}
		if (current < 0)
			return nullptr;
		for (int block = current; block < route.N_Block_Sections && block <= current + 8; ++block) {
			const Section& section = route.sequence_of_block_sections[block];
			if (section.SignallingLevel != 4 || section.ID != Last_Received_MA.BSID)
				continue;
			for (const MovementAuthority& authority : ETCS_MA)
				if (authority.BSID == section.ID && authority.type == "TrainEnd" && authority.typePart == "Tale"
					&& authority.ReversedDirection == route.reversed_direction
					&& authority.TrainInfo.trainDescription != trainDescription
					&& authority.TrainInfo.trainDescription == Last_Received_MA.TrainInfo.trainDescription
					&& Last_Received_MA.type == authority.type && Last_Received_MA.typePart == authority.typePart
					&& Last_Received_MA.ReversedDirection == authority.ReversedDirection
					&& abs(authority.AbsPosEoA - Last_Received_MA.AbsPosEoA) < 0.001
					&& (authority.TrainInfo.NextSectionID == "None"
						|| (block + 1 < route.N_Block_Sections && authority.TrainInfo.NextSectionID == route.sequence_of_block_sections[block + 1].ID))) {
					leaderHead = authorityRoutePosition(authority, section);
					if (leaderHead > head)
						return &authority;
				}
		}
		return nullptr;
	}

	// Validate before current target selection so an invalid old hold cannot clear new metadata.
	void checkIfMovementAuthorityStillValid(double speed, double head, const Section& section) {
		if (!IsTrainStoppedForEoA)
			return;
		const MovementAuthority& saved = Last_MA_StoppedAt;
		const double distance = saved.RelativePosEoA - head;
		const bool applicable = obeysAuthority(saved) && (saved.type != "TrainEnd" || (saved.typePart == "Tale" && saved.ReversedDirection == train_route[indexOfRoute].reversed_direction) || (saved.typePart == "Front" && saved.ReversedDirection != train_route[indexOfRoute].reversed_direction));
		bool valid = false;
		if (speed == 0 && distance >= 0 && distance < 4 && section.ID == saved.BSID && applicable)
			for (const MovementAuthority& authority : ETCS_MA)
				if (authority.BSID == saved.BSID && authority.type == saved.type && authority.typePart == saved.typePart
					&& authority.ReversedDirection == saved.ReversedDirection && obeysAuthority(authority)
					&& authority.TrainInfo.trainDescription == saved.TrainInfo.trainDescription
					&& abs(authority.AbsPosEoA - saved.AbsPosEoA) < 0.001) {
					valid = true;
					break;
				}
		IsTrainStoppedForEoA = valid;
		if (!valid)
			BrakingForEoA = false;
	}

	// A leader that reports no movement authority any more cannot be followed. Its last one would keep
	// this train in following mode, with the leader's last speed and acceleration as its target.
	void leaveFollowingModeWhenLeaderIsGone(int time_seconds) {
		if (IsTrainInFollowingMode == 0)
			return;
		for (const MovementAuthority& authority : ETCS_MA)
			if (authority.TrainInfo.trainDescription == LeadingTrainInFollowingMode)
				return;
		EG_LOG(SimEvents, Info) << "At time instant " << time_seconds << " train " << trainDescription << " is decoupled from train " << LeadingTrainInFollowingMode << ", which reports no position any more\n";
		VCmsgTimestep.push_back(time_seconds);
		VCmsgTrain.push_back(trainDescription);
		VCmsgText.push_back("Train is now decoupled");
		MovementAuthority ResetPredictedMAToDecoupleAt;
		IsTrainInFollowingMode = false;
		LeadingTrainInFollowingMode = "None";
		IsTrainDecoupling = false;
		Predicted_MA_To_DecoupleAt = ResetPredictedMAToDecoupleAt;
		IsInUnintentionalDecoupling = false;
		CounterFollowingMode = 0;
	}

	// Function to compute the braking distance in a faster way using the closed formula and average gradient and curvature values (This function has shown the best match to simulated braking curves)
	virtual double BrakingDistanceFastComputation(double V1, double V2, double X1, double X2, Section* BS, int N_BS) { // V1 and X1 are the current train speed and position while X2 and V2 are the target position and speed, signalling_block_sections and N_BS are the list of Block Sections and the number of Block Sections along the train route
		double AverageGradient = 0;
		double AverageCurvature = 0;
		int N_arcs = 0;

		for (int i = 0; i < N_BS; i++) {
			for (int j = 0; j < BS[i].total_arcs; j++) {
				if ((BS[i].arcs_in_signalling_block_section[j].endNode.X * 1000 > X1) && (BS[i].arcs_in_signalling_block_section[j].endNode.X * 1000 <= X2)) {
					AverageGradient = AverageGradient + BS[i].arcs_in_signalling_block_section[j].gradient;
					AverageCurvature = AverageCurvature + BS[i].arcs_in_signalling_block_section[j].curvature;
					N_arcs++;
				}
			}
		}
		// Computing Average Gradient and Average Curvature Radii
		AverageGradient = AverageGradient / N_arcs;
		AverageCurvature = AverageCurvature / N_arcs;

		// Computing the braking curve with closed formula
		double BrakingDistance;
		// BrakingDistance = V1 * timestep + (max_train_decelaration + total_train_resistances(((V1 + V2) / (2)), AverageGradient, AverageCurvature) / (total_train_mass*massFactor))*(V1 + V2) / (2 * Jerk) + (pow(V1, 2) - pow(V2, 2)) / (2 * (max_train_decelaration + total_train_resistances(((V1 + V2) / (2 * Jerk)), AverageGradient, AverageCurvature) / (total_train_mass*massFactor)));   //this formula is the full closed braking curve formula

		BrakingDistance = (pow(V1, 2) - pow(V2, 2)) / (2 * (max_train_decelaration + total_train_resistances(((V1 + V2) / 2), AverageGradient, AverageCurvature) / (total_train_mass * massFactor))); // this one resulted to be the best in terms of closeness to simulation results

		return BrakingDistance;
	}

	// Function to quickly compute the average acceleration distance between two speeds
	virtual double AccelerationTimeFollowingMode(double V1, double V2, double X1, double X2, Section* BS, int N_BS) { // V1 and X1 are the current train speed and position while X2 and V2 are the target position and speed, signalling_block_sections and N_BS are the list of Block Sections and the number of Block Sections along the train route
		double AverageGradient = 0;
		double AverageCurvature = 0;
		int N_arcs = 0;

		for (int i = 0; i < N_BS; i++) {

			for (int j = 0; j < BS[i].total_arcs; j++) {

				if ((BS[i].arcs_in_signalling_block_section[j].endNode.X * 1000 > X1) && (BS[i].arcs_in_signalling_block_section[j].endNode.X * 1000 <= X2)) {

					AverageGradient = AverageGradient + BS[i].arcs_in_signalling_block_section[j].gradient;

					AverageCurvature = AverageCurvature + BS[i].arcs_in_signalling_block_section[j].curvature;

					N_arcs++;
				}
			}
		}
		// Computing Average Gradient and Average Curvature Radii
		AverageGradient = AverageGradient / N_arcs;
		AverageCurvature = AverageCurvature / N_arcs;

		// Computing the acceleration curve of the train

		double AccelerationDistance = 0;
		double AccelerationTime = 0;

		// Splitting the speed difference into several intervals
		double V_Step = -1;
		int N_V_Steps = 200;
		if (V2 > V1) {
			V_Step = (V2 - V1) / N_V_Steps;
		} else {
			EG_LOG(SimEvents, Error) << "ERROR: Train " << this->trainDescription << " is trying to compute an average acceleration distance with a target speed V2 lower than the starting speed V1. A braking curve should be computed in this case. Please check the reason why this is happening\n";
		}

		if (V_Step > 0) {
			for (int i = 0; i <= N_V_Steps; i++) {
				double V_Current_Interval = V1 + i * V_Step;
				double V_Previous_Interval = 0;
				if (i == 0) {
					V_Previous_Interval = V1;
				} else {
					V_Previous_Interval = V1 + (i - 1) * V_Step;
				}

				// Computation of piecewise acceleration distance
				double StepAcceleration = 0;
				StepAcceleration = (this->tractiveEffort(V_Previous_Interval) - this->total_train_resistances(V_Previous_Interval, AverageGradient, AverageCurvature)) / (total_train_mass * massFactor);
				AccelerationDistance = AccelerationDistance + (pow(V_Current_Interval, 2) - pow(V_Previous_Interval, 2)) / (2 * StepAcceleration);
				AccelerationTime = AccelerationTime + (V_Current_Interval - V_Previous_Interval) / StepAcceleration;
			}
		}

		if (AccelerationTime <= 0) {
			// if the Acceleration Distance was negative that probably meant that the average Gradient was too high for the train to accelerate and the locomotive was not powerful enough
			// In this case we create a rescue acceleration distance that considers the gradient flat, i.e. =0
			double RescueAccelerationDistance = 0;
			double RescueAccelerationTime = 0;

			if (V_Step > 0) {
				for (int i = 0; i <= N_V_Steps; i++) {
					double V_Current_Interval = V1 + i * V_Step;
					double V_Previous_Interval = 0;
					if (i == 0) {
						V_Previous_Interval = V1;
					} else {
						V_Previous_Interval = V1 + (i - 1) * V_Step;
					}

					// Computation of piecewise acceleration distance
					double StepAccelerationOnFlatGround = 0;
					StepAccelerationOnFlatGround = (this->tractiveEffort(V_Previous_Interval) - this->total_train_resistances(V_Previous_Interval, 0, AverageCurvature)) / (total_train_mass * massFactor);
					RescueAccelerationDistance = RescueAccelerationDistance + (pow(V_Current_Interval, 2) - pow(V_Previous_Interval, 2)) / (2 * StepAccelerationOnFlatGround);
					RescueAccelerationTime = RescueAccelerationTime + (V_Current_Interval - V_Previous_Interval) / StepAccelerationOnFlatGround;
				}
			}

			if (RescueAccelerationTime > 0) {
				AccelerationTime = RescueAccelerationTime;
			} else { // In case also the rescue acceleration distance is negative that means that the train cannot accelerate on this section because resistances are larger than tractive effort
					 // At this point use the cruising mode for the train and the acceleration distance is 0 therefore

				EG_LOG(SimEvents, Warning) << "Train " << this->trainDescription << " has air resistances larger than tractive effort on this section with V1: " << V1 << " V2: " << V2 << " X1: " << X1 << " X2: " << X2 << ". The Cruising mode will be applied since trains cannot accelerate\n";
				AccelerationTime = -99999;

				// Then if it is because the speed difference is very small and lower than 0.1 m/s
				/*if ((V2 - V1) < 0.1) {   //then impose that the acceleration distance is 0, given that train 1 and train 2 travel almost at the same speed with an approximation of 0.1 m/s
				AccelerationDistance = 0;
				}
				else { //In case the speed difference is larger than 0.1 m/s and the rescueacceleration distance is negative then throw a Warning message
				cout << "Warning: For Train " << this->trainDescription << " a non positive approximated acceleration distance has been computed with V1: " << V1 << " V2: " << V2 << " X1: " << X1 << " X2: " << X2 << ". Please check the reason why this happened\n";
				printf_s("%.11f", V1); cout << " "; printf_s("%.11f", V2);
				cout << "\n The average gradient is: "; printf_s("%.7f", AverageGradient); cout << "\n";
				cout << "Probably the gradient is too steep for the power of the train locomotive to accelerate\n";
				}*/
			}
		}

		return AccelerationTime;
	}

	// Call-local first-match lookup. Earlier future starts invalidate forward progress,
	// so unordered and overlapping routes retain the linear scan's precedence.
	struct BrakingArcCursor {
		const Arc* arc = nullptr;
		int section = 0, arcIndex = 0;
		double lookupHead = 0;
		double earlierFuture = std::numeric_limits<double>::infinity();

		const Arc* find(double head, Section* sections, int count, double* safeUpper = nullptr) {
			if (arc && head >= lookupHead && head < earlierFuture) {
				if (head < arc->endNode.X * 1000) {
					lookupHead = head;
					if (safeUpper) *safeUpper = std::min(earlierFuture, arc->endNode.X * 1000);
					return arc;
				}
				++arcIndex;
			} else {
				section = arcIndex = 0;
				earlierFuture = std::numeric_limits<double>::infinity();
			}
			lookupHead = head;
			arc = nullptr;
			for (; section < count; ++section, arcIndex = 0)
				for (; arcIndex < sections[section].total_arcs; ++arcIndex) {
					const Arc& candidate = sections[section].arcs_in_signalling_block_section[arcIndex];
					if (head >= candidate.startNode.X * 1000 && head < candidate.endNode.X * 1000) {
						arc = &candidate;
						if (safeUpper) *safeUpper = std::min(earlierFuture, candidate.endNode.X * 1000);
						return arc;
					}
					if (candidate.startNode.X * 1000 > head)
						earlierFuture = std::min(earlierFuture, candidate.startNode.X * 1000);
				}
			if (safeUpper) *safeUpper = earlierFuture;
			return nullptr;
		}
	};

	const Arc* brakingArc(double head, Section* sections, int count, double* safeUpper = nullptr) const {
		BrakingArcCursor cursor;
		return cursor.find(head, sections, count, safeUpper);
	}

	// Every curve step samples force at the actual start and uses the right endpoint for distance.
	double brakingCurveSpeed(double speed, const Arc& arc) {
		return std::max(0.0, speed - (max_train_decelaration + total_train_resistances(speed, arc.gradient, arc.curvature) / (total_train_mass * massFactor)) * timestep);
	}

	// Strahl and Sauthoff resistance are quadratic in speed. Invert only the increasing
	// branch of v - dt*a(v), connected to the ordinary small-dt solution. The other
	// quadratic root lies beyond the fold and is not a unique inverse flow.
	// For nextSpeed == 0, q(v) == 0 selects the stopping threshold, not the
	// entire saturated preimage of max(0, q(v)); smaller terminal speeds are valid forward states.
	bool inverseBrakingStep(double nextHead, double nextSpeed, Section* sections, int count, double& head, double& speed) {
		head = nextHead - nextSpeed * timestep;
		const Arc* arc = brakingArc(head, sections, count);
		if (!arc || total_train_mass <= 0 || massFactor <= 0 || timestep <= 0 || nextSpeed < 0)
			return false;
		const double mass = total_train_mass * massFactor;
		const double r0 = total_train_resistances(0, arc->gradient, arc->curvature);
		const double r1 = total_train_resistances(1, arc->gradient, arc->curvature);
		const double r2 = total_train_resistances(2, arc->gradient, arc->curvature);
		const double quadratic = (r2 - 2 * r1 + r0) / (2 * mass) * timestep;
		const double linear = 1 - ((r1 - r0) / mass * timestep - quadratic);
		const double constant = nextSpeed + (max_train_decelaration + r0 / mass) * timestep;
		if (!std::isfinite(quadratic) || !std::isfinite(linear) || !std::isfinite(constant) || quadratic < 0)
			return false;
		if (quadratic == 0) {
			if (linear <= 0)
				return false;
			speed = constant / linear;
		} else {
			const double discriminant = linear * linear - 4 * quadratic * constant;
			if (linear <= 0 || discriminant <= 0 || !std::isfinite(discriminant))
				return false; // No finite, unique root on the increasing branch, including the fold.
			speed = 2 * constant / (linear + std::sqrt(discriminant));
			if (linear - 2 * quadratic * speed <= 0)
				return false;
		}
		if (!std::isfinite(head) || !std::isfinite(speed) || speed < 0)
			return false;
		// Refine against the actual resistance expression, avoiding cancellation in
		// the sampled coefficients. Choose the adjacent non-overshooting inverse.
		for (int iteration = 0; iteration < 4; ++iteration) {
			const double residual = speed - (max_train_decelaration + total_train_resistances(speed, arc->gradient, arc->curvature) / mass) * timestep - nextSpeed;
			const double refined = speed - residual / (linear - 2 * quadratic * speed);
			if (!std::isfinite(refined) || refined < 0 || linear - 2 * quadratic * refined <= 0)
				return false;
			if (refined == speed)
				break;
			speed = refined;
		}
		if (brakingCurveSpeed(speed, *arc) > nextSpeed)
			speed = std::nextafter(speed, 0.0);
		return brakingCurveSpeed(speed, *arc) <= nextSpeed;
	}

	// Predict complete full-force continuation after a phase-specific first step.
	// Optional vectors store only a successfully evaluated plan, in driving order.
	bool predictBraking(double head, double speed, double targetSpeed, Section* sections, int count,
		double& end, std::vector<double>* positions = nullptr, std::vector<double>* speeds = nullptr) {
		BrakingArcCursor cursor;
		for (int step = 0; step < 1999; ++step) {
			end = head; // An above-target route exit can reject a shooting bracket, never validate a plan.
			if (!std::isfinite(head) || !std::isfinite(speed) || speed < 0)
				return false;
			if (positions) {
				positions->push_back(head);
				speeds->push_back(speed);
			}
			if (speed <= targetSpeed) {
				end = head;
				return true;
			}
			const Arc* arc = cursor.find(head, sections, count);
			if (!arc)
				return false;
			speed = brakingCurveSpeed(speed, *arc);
			head += speed * timestep;
		}
		return false;
	}

	// Fixed-length forward prefix for a positive target whose native plan may dip
	// below the target and recover. Crossing below the target is not completion.
	bool predictBrakingPrefix(double head, double speed, int steps, Section* sections, int count,
		double& end, double& endSpeed, std::vector<double>* positions = nullptr, std::vector<double>* speeds = nullptr) {
		if (steps < 0 || steps >= 1999)
			return false;
		BrakingArcCursor cursor;
		for (int step = 0; step <= steps; ++step) {
			end = head;
			endSpeed = speed;
			if (!std::isfinite(head) || !std::isfinite(speed) || speed < 0)
				return false;
			if (positions) {
				positions->push_back(head);
				speeds->push_back(speed);
			}
			if (step == steps)
				return true;
			const Arc* arc = cursor.find(head, sections, count);
			if (!arc)
				return false;
			speed = brakingCurveSpeed(speed, *arc);
			head += speed * timestep;
		}
		return false;
	}

	// Upper end of the reachable terminal-coordinate interval at a positive ceiling.
	bool brakingTerminalUpper(double head, double speed, double ceiling, Section* sections, int count, double& upper) {
		const Arc* arc = brakingArc(head, sections, count);
		if (!arc) {
			upper = head;
			return false;
		}
		const double maximum = std::max(0.0, speed + (tractiveEffort(speed) - total_train_resistances(speed, arc->gradient, arc->curvature)) / (total_train_mass * massFactor) * timestep);
		upper = head + std::min(ceiling, maximum) * timestep;
		return std::isfinite(upper);
	}

	// Select a new terminal control within the actual-state force interval and ceiling.
	// Rounded addition need not be exactly inverted by subtraction and division.
	bool constructBrakingTerminal(double head, double speed, double anchor, double ceiling, Section* sections, int count, double& terminal) {
		const Arc* arc = brakingArc(head, sections, count);
		if (!arc || !std::isfinite(head) || !std::isfinite(speed) || speed < 0 || !std::isfinite(anchor)
			|| anchor < head || !std::isfinite(ceiling) || ceiling < 0 || timestep <= 0 || total_train_mass <= 0 || massFactor <= 0)
			return false;
		const double mass = total_train_mass * massFactor;
		const double resistance = total_train_resistances(speed, arc->gradient, arc->curvature), effort = tractiveEffort(speed);
		if (!std::isfinite(mass) || !std::isfinite(timestep) || !std::isfinite(resistance) || !std::isfinite(effort) || !std::isfinite(max_train_decelaration))
			return false;
		const double minimum = brakingCurveSpeed(speed, *arc);
		const double maximum = std::min(ceiling, std::max(0.0, speed + (effort - resistance) / mass * timestep));
		if (!std::isfinite(minimum) || !std::isfinite(maximum) || minimum > maximum)
			return false;
		terminal = std::clamp((anchor - head) / timestep, minimum, maximum);
		const double end = head + terminal * timestep;
		const double roundoff = 8 * std::numeric_limits<double>::epsilon() * std::max(1.0, std::fabs(anchor));
		return std::isfinite(end) && std::fabs(end - anchor) <= roundoff;
	}

	bool planPositiveBraking(double head, double speed, const Arc& arc, Section* sections, int count, int suggested) {
		const double firstMinimum = brakingCurveSpeed(speed, arc);
		const double firstMaximum = std::max(0.0, speed + (tractiveEffort(speed) - total_train_resistances(speed, arc.gradient, arc.curvature)) / (total_train_mass * massFactor) * timestep);
		const double anchor = Xobmin - 0.002;
		const double roundoff = 8 * std::numeric_limits<double>::epsilon() * std::max(1.0, std::fabs(anchor));
		double minimumNative = Vobmin;
		for (int k = 0; k <= BrakStep; ++k)
			if (Vbrak[k] > 0) minimumNative = std::min(minimumNative, Vbrak[k]);
		const double phaseSteps = std::ceil(std::max(0.0, Sbrak[0] - head) / (minimumNative * timestep));
		if (!std::isfinite(phaseSteps))
			return false;
		const int extra = phaseSteps >= 1998 ? 1999 : std::max(1, static_cast<int>(phaseSteps) + 1);
		for (int length = std::max(1, suggested - 1); length <= std::min(1999, suggested + extra); ++length) {
			double low = firstMinimum, high = firstMaximum, end, endSpeed, upper;
			if (length == 1) {
				double terminal;
				if (!constructBrakingTerminal(head, speed, anchor, Vobmin, sections, count, terminal)) continue;
				Sbrak[0] = head;
				Vbrak[0] = speed;
				Sbrak[1] = head + terminal * timestep;
				Vbrak[1] = terminal;
				BrakStep = 1;
				return true;
			}
			if (!std::isfinite(low) || !std::isfinite(high) || low > high || !predictBrakingPrefix(head + low * timestep, low, length - 2, sections, count, end, endSpeed)
				|| !brakingTerminalUpper(end, endSpeed, Vobmin, sections, count, upper) || upper > anchor + roundoff) continue;
			const bool completeUpper = predictBrakingPrefix(head + high * timestep, high, length - 2, sections, count, end, endSpeed);
			if (!completeUpper) {
				if (!(end > anchor)) continue;
				upper = end;
			} else if (!brakingTerminalUpper(end, endSpeed, Vobmin, sections, count, upper)) {
				if (!(end > anchor)) continue;
				upper = end;
			}
			if (upper < anchor) continue;
			for (int iteration = 0; iteration < 64; ++iteration) {
				const double middle = low + (high - low) / 2;
				if (middle == low || middle == high) break;
				const bool complete = predictBrakingPrefix(head + middle * timestep, middle, length - 2, sections, count, end, endSpeed);
				if (!complete || !brakingTerminalUpper(end, endSpeed, Vobmin, sections, count, upper)) {
					if (!(end > anchor)) break;
					upper = end;
				}
				if (upper < anchor) low = middle;
				else high = middle;
			}
			std::vector<double> positions{head}, speeds{speed};
			if (!predictBrakingPrefix(head + high * timestep, high, length - 2, sections, count, end, endSpeed, &positions, &speeds)) continue;
			double terminal;
			if (!constructBrakingTerminal(end, endSpeed, anchor, Vobmin, sections, count, terminal)) continue;
			positions.push_back(end + terminal * timestep);
			speeds.push_back(terminal);
			if (std::fabs(positions.back() - anchor) > roundoff || positions.size() > Sbrak.size()) continue;
			std::copy(positions.begin(), positions.end(), Sbrak.begin());
			std::copy(speeds.begin(), speeds.end(), Vbrak.begin());
			BrakStep = static_cast<int>(positions.size()) - 1;
			return true;
		}
		return false;
	}

	// Returns an actual-start braking point. Linear interpolation of inverse X/V
	// samples is not a solution of the nonlinear forward flow.
	virtual double BrakDist_Block(double V1, double V2, double X0, Section* BS, int Blocks) {
		if (V1 <= V2)
			return X0;
		double nextHead = X0 - 0.002, nextSpeed = V2;
		for (int step = 1; step < 2000; ++step) {
			double head, speed;
			if (!inverseBrakingStep(nextHead, nextSpeed, BS, Blocks, head, speed)) {
				if (head < BS[0].start_node.X * 1000)
					return head;
				return -1;
			}
			if (speed >= V1) {
				if (V2 > 0) {
					const double anchor = X0 - 0.002;
					double low = head, high = nextHead, end, endSpeed, upper;
					if (!predictBrakingPrefix(low, V1, step - 1, BS, Blocks, end, endSpeed) || !brakingTerminalUpper(end, endSpeed, V2, BS, Blocks, upper) || upper > anchor) return -1;
					const bool completeUpper = predictBrakingPrefix(high, V1, step - 1, BS, Blocks, end, endSpeed);
					if (!completeUpper || !brakingTerminalUpper(end, endSpeed, V2, BS, Blocks, upper)) {
						if (!(end > anchor)) return -1;
						upper = end;
					}
					if (upper < anchor) return -1;
					for (int iteration = 0; iteration < 64; ++iteration) {
						const double middle = low + (high - low) / 2;
						if (middle == low || middle == high) break;
						const bool complete = predictBrakingPrefix(middle, V1, step - 1, BS, Blocks, end, endSpeed);
						if (!complete || !brakingTerminalUpper(end, endSpeed, V2, BS, Blocks, upper)) {
							if (!(end > anchor)) return -1;
							upper = end;
						}
						if (upper < anchor) low = middle;
						else high = middle;
					}
					if (!predictBrakingPrefix(high, V1, step - 1, BS, Blocks, end, endSpeed)) return -1;
					double terminal;
					if (!constructBrakingTerminal(end, endSpeed, anchor, V2, BS, Blocks, terminal)) return -1;
					return high;
				}
				double low = head, high = nextHead, end;
				const double roundoff = 8 * std::numeric_limits<double>::epsilon() * std::max(1.0, std::fabs(X0 - 0.002));
				if (!predictBraking(low, V1, V2, BS, Blocks, end) || end > X0 - 0.002 + roundoff)
					return -1;
				const bool completeUpper = predictBraking(high, V1, V2, BS, Blocks, end);
				if ((!completeUpper && !(end > X0 - 0.002)) || end < X0 - 0.002 - roundoff)
					return -1;
				for (int iteration = 0; iteration < 64; ++iteration) {
					const double middle = low + (high - low) / 2;
					if (middle == low || middle == high)
						break;
					if (!predictBraking(middle, V1, V2, BS, Blocks, end) && !(end > X0 - 0.002))
						return -1;
					if (end <= X0 - 0.002)
						low = middle;
					else
						high = middle;
				}
				return low; // Conservative side of a boundary discontinuity, not a target snap.
			}
			nextHead = head;
			nextSpeed = speed;
		}
		return -1;
	}

	// Request before the next movement can consume the last opportunity to brake for the entry. The movement
	// bound uses the same force/resistance model and starting-effort bound as acceleration and virtual coupling.
	bool needsSingleTrackReservation(double head, double speed, double entry, Section* sections, int count) {
		if (head >= entry)
			return true;
		if (!std::isfinite(speed) || speed < 0 || total_train_mass <= 0 || massFactor <= 0 || !std::isfinite(max_train_speed))
			return true;
		double nextSpeed = speed;
		bool foundArc = false;
		for (int b = 0; b < count && !foundArc; ++b)
			for (int a = 0; a < sections[b].total_arcs; ++a) {
				const Arc& arc = sections[b].arcs_in_signalling_block_section[a];
				if (head < arc.startNode.X * 1000 || head >= arc.endNode.X * 1000)
					continue;
				const double acceleration = (std::max(tractiveEffort(speed), tractiveEffort(0))
												- total_train_resistances(speed, arc.gradient, arc.curvature))
					/ (total_train_mass * massFactor);
				if (!std::isfinite(acceleration))
					return true; // no safe movement bound
				nextSpeed = std::min(max_train_speed, speed + std::max(0.0, acceleration) * timestep);
				foundArc = true;
				break;
			}
		if (!foundArc || !std::isfinite(nextSpeed))
			return true;
		const double nextHead = head + std::max(speed, nextSpeed) * timestep;
		if (nextHead >= entry)
			return true;
		auto needsBraking = [&](double target, double targetSpeed) {
			if (nextSpeed <= targetSpeed)
				return false;
			if (nextHead >= target)
				return true;
			const double brakingStart = BrakDist_Block(nextSpeed, targetSpeed, target, sections, count);
			if (!std::isfinite(brakingStart) || brakingStart == -1)
				return true; // a failed lookup cannot establish a later safe request
			if (brakingStart < sections[0].start_node.X * 1000)
				return true; // the last braking opportunity precedes the route origin
			return nextHead >= brakingStart;
		};
		// Fixed-block locks also constrain the approach blocks. Reserve before those speed targets become too late,
		// rather than introducing a speed cap on a train already in the block carrying it.
		for (int b = 0; b < count; ++b)
			if (sections[b].start_node.X * 1000 == entry) {
				if (b > 0 && sections[b - 1].SignallingLevel == 0
					&& needsBraking(sections[b - 1].start_node.X * 1000, signalCode1))
					return true;
				if (b > 0 && sections[b - 1].SignallingLevel == 5
					&& needsBraking(sections[b - 1].start_node.X * 1000, 0))
					return true;
				if (b > 1 && sections[b - 2].SignallingLevel == 5
					&& needsBraking(sections[b - 2].start_node.X * 1000, signalCode1))
					return true;
				break;
			}
		return needsBraking(entry, 0);
	}

	// A cached successor is a proposal only from its exact actual state and historical target.
	int cachedBrakingSuccessor(int previousInstant, const Arc& arc, double target, double targetSpeed) {
		if (counter <= 0 || BrakStep <= 0 || BrakStep >= static_cast<int>(Sbrak.size())
			|| (previousInstant < static_cast<int>(Xob.size()) && (Xob[previousInstant] != target || Vob[previousInstant] != targetSpeed)))
			return -1;
		const double head = instant_spatial_position[previousInstant], speed = instant_train_speed[previousInstant];
		for (int k = 0; k < BrakStep; ++k) {
			if (head != Sbrak[k] || speed != Vbrak[k])
				continue;
			const double nextSpeed = k == 0 || (targetSpeed > 0 && k + 1 == BrakStep) ? Vbrak[k + 1] : brakingCurveSpeed(speed, arc);
			const double nextHead = head + nextSpeed * timestep;
			const double minimum = brakingCurveSpeed(speed, arc);
			const double maximum = std::max(0.0, speed + (tractiveEffort(speed) - total_train_resistances(speed, arc.gradient, arc.curvature)) / (total_train_mass * massFactor) * timestep);
			if (nextSpeed < minimum || nextSpeed > maximum || nextHead != Sbrak[k + 1] || nextSpeed != Vbrak[k + 1])
				return -1;
			return k + 1;
		}
		return -1;
	}

	// This Function simulate the capacity of the train driver to react to the first most severe service limit (i.e. it calculates the braking distance and the braking point for satisfying the most severe running operation limit)
	double europeanVitalComputerWithListsImproved(double S, double V, int PreviousInstant, Section* BS, int Blocks, bool& continueCached) {
		continueCached = false;
		IsTrainCoupling = false;
		int N_BrakP;
		int ArcPos = 0;
		int BlockPos = 0;
		double MinBX = 99999999999; // Number of Braking points in the Block Section occupied by the train, Index of the Arc of the signalling_block_sections occupied by the train, Index of the signalling_block_sections occupied by the train, Matrix of all Brak Distance of the Braking point of the Block Section occupied by the train, Minimum Braking Abscissa, Braking Abscissa of the block section arcs which follows the signalling_block_sections occupied by the train
		Node ClosestETCSBrakingPoint;
		ClosestETCSBrakingPoint.X = 9999999999;			 // This is the closest ETCS braking point. In this case the closestpoint is not the MA having teh closest abscissa to the current position of the train but it is the MA which requirest the train to brake the earliest to be met
		double TargetSpeedForMA = -1;					 // This is the Speed the train needs to target to meet the most restrictive MA
		double ClosestPointToBrakeForETCS = 9999999999;	 // This is the value of the closese Point to Brake For ETCS
		int SignallingLevelClosestETCSBrakingPoint = -1; // This is the signalling level of the closest ETCS Braking Point

		Node ETCSBrakingPoint; // Node to collect all the information of the ETCS BrakingPoints

		list<Node> BrakingPoints; // This is the list of the nodes constituting braking points

		for (int h = 0; h < Blocks; h++) {
			if ((S < BS[h].end_node.X * 1000) && (S >= BS[h].start_node.X * 1000)) {
				BlockPos = h;
				break;
			}
		}
		for (int j = 0; j < BS[BlockPos].total_arcs; j++) {
			if ((S < BS[BlockPos].arcs_in_signalling_block_section[j].endNode.X * 1000) && (S >= BS[BlockPos].arcs_in_signalling_block_section[j].startNode.X * 1000)) {
				ArcPos = j;
				break;
			}
		}
		N_BrakP = BS[BlockPos].total_arcs - ArcPos; // Initializing dimensions of Braking points vectors

		// Putting in the list of braking points the nodes of the Arc where the train currently is
		for (int p = ArcPos; p < BS[BlockPos].total_arcs; p++) {
			// Setting the current Arc speedInBraking to the final Node of Arc
			BS[BlockPos].arcs_in_signalling_block_section[p].endNode.arcSpeedLimit = BS[BlockPos].arcs_in_signalling_block_section[p].speedInBraking;
			BrakingPoints.push_back(BS[BlockPos].arcs_in_signalling_block_section[p].endNode);
		}

		// Calculating the total number of breaking points to find the minimum breaking distance (all the arcs of the block section which follows the occupied one are also considered)
		int Cons_Block = 8;
		if (BlockPos + Cons_Block > Blocks - 1) {
			Cons_Block = Blocks - BlockPos - 1;
		}

		// Putting in list of Braking Points all the nodes belonging to the other block sections
		for (int z = 0; z < Cons_Block; z++) {
			for (int p = 0; p < BS[z + BlockPos + 1].total_arcs; p++) {
				BS[z + BlockPos + 1].arcs_in_signalling_block_section[p].endNode.arcSpeedLimit = BS[z + BlockPos + 1].arcs_in_signalling_block_section[p].speedInBraking;
				BrakingPoints.push_back(BS[z + BlockPos + 1].arcs_in_signalling_block_section[p].endNode);
			}
		}

		// Identification of the closest EoA where the train needs to brake at (i.e. the EoA for which the train needs to brake the earliest to meet it)
		for (int z = 0; z <= Cons_Block; z++) {
			// A SignalFailure EoA applies at every signalling level, so scan the MA list on any section carrying one
			bool hasSignalFailureMa = false;
			for (list<MovementAuthority>::iterator it = ETCS_MA.begin(); it != ETCS_MA.end(); it++) {
				if ((it->type == "SignalFailure") && (it->BSID == BS[z + BlockPos].ID) && obeysAuthority(*it)) {
					hasSignalFailureMa = true;
					break;
				}
			}
			if ((BS[z + BlockPos].SignallingLevel == 3) || (BS[z + BlockPos].SignallingLevel == 4) || hasSignalFailureMa) {
				// Identifying Braking Points computed by the EVC if the train is virtually coupled to a train ahead
				if (IsTrainInFollowingMode == 1) { // if the train is in following mode
					if ((Predicted_MA_To_DecoupleAt.TrainInfo.trainDescription != "None") && (Predicted_MA_To_DecoupleAt.TrainInfo.trainDescription == LeadingTrainInFollowingMode)) {
						if (BS[z + BlockPos].ID == Predicted_MA_To_DecoupleAt.BSID) {
							Node DecouplingBrakingPoint;
							DecouplingBrakingPoint.X = Predicted_MA_To_DecoupleAt.RelativePosEoA;
							DecouplingBrakingPoint.isSignalled = true;
							DecouplingBrakingPoint.stationName = "DecoupleTrain"; // For a Decoupling Point as a convention we say that it has a station name DecoupleTrain. We did so not to increase the number of variables of the class Node
							BrakingPoints.push_back(DecouplingBrakingPoint);
						}
					}
				}
				// Now identifying braking points due to ETCS MA
				if (ETCS_MA.size() > 0) {
					for (list<MovementAuthority>::iterator it = ETCS_MA.begin(); it != ETCS_MA.end(); it++) {
						if ((it->BSID == BS[z + BlockPos].ID) && (it->TrainInfo.trainDescription != trainDescription) && obeysAuthority(*it)) {
							double AbscissaToBrakeAtForEoA = -1;	// This is the progressive the train needs to start braking at to meet the EoA
							double SpeedForEoA = -1;				// This is the speed to be met to respect the MA
							bool BrakingForEoAModified = false;		// This variable is true when we modify the BrakingPoint because of a prediction distance to allow the train to couple with the one ahead
							bool CanTrainLinkForCoupling = false;	// This variable is a local variable to state whether the train is linking to another train ahead for virtual coupling. This boolean is true only when the closest breaking point is the Train End of a train in the same direction having the next Route section in common on a Bs with Signalling Level 4
							double PredictedDistanceToCoupling = 0; // This is the Distance need by the train to Couple with the train ahead

							ETCSBrakingPoint.X = authorityRoutePosition(*it, BS[z + BlockPos]);
							// A behind or front end cannot become a forward leader through virtual projection.
							if (BS[z + BlockPos].SignallingLevel == 4 && it->type == "TrainEnd"
								&& it->ReversedDirection == train_route[indexOfRoute].reversed_direction
								&& (it->typePart != "Tale" || !(ETCSBrakingPoint.X > S)))
								continue;

							if (it->type == "SignalFailure") {
								// a failed signal or a held single-track section is a stop target at every signalling level
								SpeedForEoA = 0;
							} else if (BS[z + BlockPos].SignallingLevel == 3) {
								SpeedForEoA = 0;

							} else if (BS[z + BlockPos].SignallingLevel == 4) {
								string NextBlockID;
								if (z + BlockPos == train_route[this->indexOfRoute].N_Block_Sections - 1) { // if the block is the last of the train route then the ID of the Next Block is None
									NextBlockID = "None";
								} else {
									NextBlockID = BS[z + BlockPos + 1].ID;
								}
								if (it->type == "DivergingSwitchOccupied") {
									if ((it->TrainInfo.NextSectionID != NextBlockID) || ((it->TrainInfo.CurrentSectionID != BS[z + BlockPos].ID) && (it->TrainInfo.NextSectionID == NextBlockID))) {
										SpeedForEoA = 0;
									}
								} else if (it->type == "TrainEnd") {
									// In case the MA is a Train End the Speed shall be 0 if the train ahead is going in the opposite direction
									if (it->ReversedDirection != train_route[this->indexOfRoute].reversed_direction) {
										SpeedForEoA = 0;

									}
									// Otherwise
									else {
										if ((it->TrainInfo.NextSectionID == NextBlockID) || (it->TrainInfo.NextSectionID == "None")) { // when the trains have the same next block section or the leading train is approahing the end of its route
											SpeedForEoA = it->TrainInfo.TrainSpeed;													   // the speed is the one of the train ahead if the trains are following the same direction and ahve at least one more block section in common

											if ((this->IsTrainInFollowingMode == 0) && (it->TrainInfo.Acceleration < 0)) {
												// A leader that brakes stops where this train would stop from the speed of the leader. A train that is not coupled yet
												// stops behind that point instead of matching the speed of the leader. Without a braking distance (-1) the end of
												// authority stays where the leader reported it.
												const double LeaderBrakingStart = BrakDist_Block(it->TrainInfo.TrainSpeed, 0, ETCSBrakingPoint.X, BS, Blocks);
												// The inverse ends at X - 0.002; its holdback is not forward travel.
												const double LeaderStandstill = LeaderBrakingStart < 0 || it->TrainInfo.TrainSpeed == 0
													? ETCSBrakingPoint.X
													: std::min(ETCSBrakingPoint.X + ((ETCSBrakingPoint.X - 0.002) - LeaderBrakingStart), train_route[this->indexOfRoute].x_of_end_node * 1000);
												PredictedDistanceToCoupling = LeaderStandstill - ETCSBrakingPoint.X;
												ETCSBrakingPoint.X = LeaderStandstill;
												BrakingForEoAModified = true;
												SpeedForEoA = 0;
											} else if ((this->IsTrainInFollowingMode == 0) || ((this->IsInUnintentionalDecoupling == 1) && (it->TrainInfo.Acceleration >= 0))) { // if the train is not yet in following mode then
												if (V > SpeedForEoA) {
													CanTrainLinkForCoupling = true;									   // This is the only case when trains can start linking to be virtually coupled, if they are not coupled already
													double TimeToBrake = ((V - SpeedForEoA) / max_train_decelaration); // +(1 * timestep);  //This is the time needed by the train to reach the speed of the train ahead, plus 1 is needed to cover up for the integration delay of 1 step
													PredictedDistanceToCoupling = it->TrainInfo.TrainSpeed * TimeToBrake;
													// With the statement below trains will always try to couple with a train ahead if that is possible
													if ((ETCSBrakingPoint.X + PredictedDistanceToCoupling) <= train_route[this->indexOfRoute].x_of_end_node * 1000) { // if with the predicted distance the Braking Point is still within the limits of train route then
														ETCSBrakingPoint.X = ETCSBrakingPoint.X + PredictedDistanceToCoupling;										  // Change the distance of the BrakingPoint. We chang it here so that the function can also detect other EoA for other train that might pop up between the real BrakingPoint.X and the prediction distance
														BrakingForEoAModified = true;
													}
												}
											}
										} else { // or if the trains do not have the next block section in common the trains need to split apart and this train will separate from the previous by means of an absolute braking distance
											SpeedForEoA = 0;
										}
									}
								}
							}

							if (SpeedForEoA > -1) { // if we have a valid MA and the SpeedForEoa is therefore different from -1
								// let us compute the approximate abscissa to Brake at for EoA
								AbscissaToBrakeAtForEoA = ETCSBrakingPoint.X - (pow(V, 2) - pow(SpeedForEoA, 2) / (2 * this->max_train_decelaration));

								if ((ETCSBrakingPoint.X > S) && (AbscissaToBrakeAtForEoA < ClosestPointToBrakeForETCS)) {
									ClosestPointToBrakeForETCS = AbscissaToBrakeAtForEoA; // if the AbscissaToBrakeAtForEoA is lower then ClosestPointToBrakeForETCS then this one becomes the ClosestPointToBrakeAt
									TargetSpeedForMA = SpeedForEoA;						  // The TargetSpeed for MA becomes the Speed ForEoA
									ClosestETCSBrakingPoint = ETCSBrakingPoint;
									ClosestETCSBrakingPoint.isSignalled = true; // putting isSignalled to true, gives a way to the Braking point to be recognised as an End of Authority
									SignallingLevelClosestETCSBrakingPoint = BS[z + BlockPos].SignallingLevel;

									// if the train can link with a train ahead
									ClosestETCSBrakingPoint.virtualCouplingNode = CanTrainLinkForCoupling;
									// Assigning the last received MA
									this->Last_Received_MA = *it;
									Last_Received_MA.RelativePosEoA = ETCSBrakingPoint.X; // Setting the Relative position of the EoA on the route of the train
									// In case the distance of ETCSBrakingPoint was modified then we need to subtract the Predicted Distance from it
									if (BrakingForEoAModified == 1) {
										Last_Received_MA.RelativePosEoA = ETCSBrakingPoint.X - PredictedDistanceToCoupling;
									}
									if (ClosestETCSBrakingPoint.virtualCouplingNode == 1) { // In case the train is not in following mode and can couple at a train ahead
										// Assigning the Predicted MA to link at
										this->Predicted_MA_To_CoupleAt = *it;
										Predicted_MA_To_CoupleAt.RelativePosEoA = ETCSBrakingPoint.X;
									}
								}
							}
						}
					}
				}
			}
		}
		// if there is a Closest ETCS Braking point that has been assigned, then add it to the list of braking points
		if (ClosestETCSBrakingPoint.X != 9999999999) {
			BrakingPoints.push_back(ClosestETCSBrakingPoint);
		}

		// Resolve each collected target once, before deciding whether the incumbent is still live.
		struct Target {
			Node node;
			double position, speed;
		};
		std::vector<Target> targets;
		int incumbent = -1;
		for (const Node& node : BrakingPoints) {
			const bool mustStop = Train_Must_Stop_For_OL_Order(node.respectOrder, node.indexOrderList);
			const double position = node.isSignalled && !mustStop ? node.X : node.X * 1000;
			const double speed = mustStop || (node.isSignalled && node.stationName == "DecoupleTrain") ? 0
				: node.isSignalled																	   ? TargetSpeedForMA
																									   : node.arcSpeedLimit;
			targets.push_back({node, position, speed});
			if (position > S && position == Xob[PreviousInstant] && speed == Vob[PreviousInstant])
				incumbent = static_cast<int>(targets.size()) - 1;
		}
		const Arc& arc = BS[BlockPos].arcs_in_signalling_block_section[ArcPos];
		const int successor = incumbent < 0 ? -1 : cachedBrakingSuccessor(PreviousInstant, arc, targets[incumbent].position, targets[incumbent].speed);
		const double proposedSpeed = successor < 0 ? V : Vbrak[successor];
		const double proposedHead = successor < 0 ? S : Sbrak[successor];
		int selected = -1;
		for (int index = 0; index < static_cast<int>(targets.size()); ++index) {
			const Target& target = targets[index];
			if (target.position <= S || (target.speed > 0 && proposedSpeed <= target.speed))
				continue;
			// A retained stop cannot cross a target beyond its zero-speed endpoint.
			if (successor >= 0 && targets[incumbent].speed == 0 && Vbrak[BrakStep] == 0
				&& Sbrak[BrakStep] < target.position)
				continue;
			double point = BrakDist_Block(proposedSpeed, target.speed, target.position, BS, Blocks);
			if (successor >= 0) {
				if (index == incumbent)
					continue;
				if (target.speed == 0) {
					double end;
					const double anchor = target.position - 0.002;
					const double roundoff = 8 * std::numeric_limits<double>::epsilon() * std::max(1.0, std::fabs(anchor));
					if (predictBraking(proposedHead, proposedSpeed, 0, BS, Blocks, end) && end <= anchor + roundoff)
						continue;
				} else if (std::isfinite(point) && point != -1 && proposedHead < point)
					continue;
				// A missing prediction cannot establish that the proposed successor is safe.
				if (!std::isfinite(point) || point == -1)
					point = S;
			} else if (target.node.isSignalled && target.speed == 0 && S + V * timestep >= target.position
				&& (point == -1 || point > S))
				point = S;
			if (point != -1 && point < MinBX) {
				MinBX = point;
				selected = index;
			}
		}
		// An incompatible successor must be interrupted now, before any nominal acceleration.
		if (successor >= 0 && selected >= 0)
			MinBX = std::min(MinBX, S);
		if (successor >= 0 && selected < 0) {
			selected = incumbent;
			continueCached = true;
			MinBX = BrakDist_Block(V, targets[incumbent].speed, targets[incumbent].position, BS, Blocks);
			if (MinBX == -1)
				MinBX = S;
		}
		if (selected >= 0) {
			const Target& target = targets[selected];
			Xobmin = target.position;
			Vobmin = target.speed;
			BrakingForEoA = target.node.isSignalled;
			IsTrainCoupling = target.node.virtualCouplingNode;
			if (IsTrainInFollowingMode == 1)
				IsTrainDecoupling = target.node.stationName == "DecoupleTrain";
		}
		return MinBX;
	}

	// Native inverse plan shares the exact actual-start step with BrakDist_Block.
	virtual bool DrawBrakingCurve(double V1, double V2, double X0, Section* BS, int Blocks) {
		std::vector<double> positions{X0 - 0.002}, speeds{V2};
		BrakStep = -1;
		if (V1 <= V2)
			return false;
		for (int step = 1; step < 2000; ++step) {
			double head, speed;
			if (!inverseBrakingStep(positions.back(), speeds.back(), BS, Blocks, head, speed))
				return false;
			positions.push_back(head);
			speeds.push_back(speed);
			if (speed >= V1) {
				BrakStep = step;
				std::reverse_copy(positions.begin(), positions.end(), Sbrak.begin());
				std::reverse_copy(speeds.begin(), speeds.end(), Vbrak.begin());
				return true;
			}
		}
		return false;
	}

	// Establish a first speed within the actual force range, then predict every
	// subsequent full model step. No linear fractional phase is carried forward.
	bool planBraking(double head, double speed, const Arc& arc, Section* sections, int count) {
		const double mass = total_train_mass * massFactor;
		const double resistance = total_train_resistances(speed, arc.gradient, arc.curvature);
		double low = brakingCurveSpeed(speed, arc);
		double high = std::max(0.0, speed + (tractiveEffort(speed) - resistance) / mass * timestep);
		const double anchor = Xobmin - 0.002;
		const double roundoff = 8 * std::numeric_limits<double>::epsilon() * std::max(1.0, std::fabs(anchor));
		double end;
		if (!std::isfinite(low) || !std::isfinite(high) || high < low
			|| !predictBraking(head + low * timestep, low, Vobmin, sections, count, end) || end > anchor + roundoff)
			return false;
		if (std::fabs(end - anchor) > roundoff) {
			const bool completeUpper = predictBraking(head + high * timestep, high, Vobmin, sections, count, end);
			if ((!completeUpper && !(end > anchor)) || end < anchor)
				return false;
			for (int iteration = 0; iteration < 64; ++iteration) {
				const double middle = low + (high - low) / 2;
				if (middle == low || middle == high)
					break;
				if (!predictBraking(head + middle * timestep, middle, Vobmin, sections, count, end) && !(end > anchor))
					return false;
				if (end <= anchor)
					low = middle;
				else
					high = middle;
			}
		}
		std::vector<double> positions{head}, speeds{speed};
		if (!predictBraking(head + low * timestep, low, Vobmin, sections, count, end, &positions, &speeds))
			return false;
		// Roundoff only. A gap caused by an arc discontinuity is not a feasible stop.
		if (Vobmin == 0 && std::fabs(end - anchor) > roundoff)
			return false;
		if (positions.size() > Sbrak.size())
			return false;
		std::copy(positions.begin(), positions.end(), Sbrak.begin());
		std::copy(speeds.begin(), speeds.end(), Vbrak.begin());
		BrakStep = static_cast<int>(positions.size()) - 1;
		return true;
	}

	void brakingStep(int time_seconds, const Arc& As, Section* BS, int Blocks) {
		const double head = instant_spatial_position[time_seconds - 1];
		const double speed = instant_train_speed[time_seconds - 1];
		const bool changedTarget = time_seconds - 1 < static_cast<int>(Xob.size())
			&& (Xob[time_seconds - 1] != Xobmin || Vob[time_seconds - 1] != Vobmin);
		// Retain a native or forward plan only at a real stored state, never by interpolation.
		const int successor = cachedBrakingSuccessor(time_seconds - 1, As, Xobmin, Vobmin);
		if (successor >= 0) {
			instant_train_speed[time_seconds] = Vbrak[successor];
			instant_spatial_position[time_seconds] = Sbrak[successor];
			brakingPoint = successor;
			Eq[time_seconds] = 52;
			GradientExceptionInBraking = Vbrak[successor] < Vobmin && successor < BrakStep;
			return;
		}
		const bool newCurve = counter == 0 || changedTarget;
		const double coverage = std::max(speed, std::max(0.0, speed + (tractiveEffort(speed) - total_train_resistances(speed, As.gradient, As.curvature)) / (total_train_mass * massFactor) * timestep));
		const bool established = !newCurve || DrawBrakingCurve(coverage, Vobmin, Xobmin, BS, Blocks);
		int remaining = BrakStep;
		for (int k = 0; k < BrakStep; ++k)
			if (head >= Sbrak[k] && head < Sbrak[k + 1]) {
				remaining = BrakStep - k;
				break;
			}
		const bool planned = established && (Vobmin > 0 ? planPositiveBraking(head, speed, As, BS, Blocks, remaining) : planBraking(head, speed, As, BS, Blocks));
		if (planned) {
			instant_train_speed[time_seconds] = Vbrak[1];
			instant_spatial_position[time_seconds] = Sbrak[1];
			brakingPoint = 1;
			Eq[time_seconds] = newCurve ? 55 : 53;
			counter = 1;
			GradientExceptionInBraking = Vbrak[1] < Vobmin && BrakStep > 1;
			return;
		}
		fullBrakingStep(time_seconds, As);
		counter = 0;
		BrakStep = -1;
	}

	// One step of braking with the full braking force, for a train that has no braking curve to follow.
	// The deceleration is the one of the braking curve: the full braking force plus the resistances at the speed of the train and the gradient and
	// curvature of the arc As, divided by the mass with its mass factor. On a descent steeper than the braking force holds it is negative and the train gains speed.
	// The new speed is not below zero and the train advances by the mean of the old and the new speed.
	void fullBrakingStep(int time_seconds, const Arc& As) {
		const double speed = instant_train_speed[time_seconds - 1];
		const double mass = total_train_mass * massFactor;
		const double deceleration = (mass * max_train_decelaration + total_train_resistances(speed, As.gradient, As.curvature)) / mass;
		instant_train_speed[time_seconds] = std::max(0.0, speed - deceleration * timestep);
		instant_spatial_position[time_seconds] = instant_spatial_position[time_seconds - 1] + (speed + instant_train_speed[time_seconds]) / 2 * timestep;
		GradientExceptionInBraking = false;
		Eq[time_seconds] = 54;
	}

	// Function for printing Train Trajectory Data
	virtual void PrintTrajectory() {
		string path;
		path = path + initial_variables.OutputMainFolder + "/TEMP/Traj_Train_" + trainDescription + ".txt";
		ofstream trainout;
		trainout.open((char*)path.c_str(), ios::binary);
		trainout << "Time[s]\tSpeed[m/s]\tPosition[m]\tTail_Position[m]\tPower_Cons[kW]\tBX[m]\tinstant_train_energy_consumption[KWh]\tBlock" << "\n";
		const auto segments = validTrajectorySegments(instant_spatial_position, earliestActiveTrajectoryIndex, End_Time);
		bool firstSegment = true;
		if (train_route[indexOfRoute].reversed_direction == 0) {
			for (const auto& segment : segments) {
				if (!firstSegment)
					trainout << "\n";
				firstSegment = false;
				for (int i = segment.first; i <= segment.last; i++) {
					trainout << i * timestep << "\t" << instant_train_speed[i] << "\t" << instant_spatial_position[i] << "\t" << instant_spatial_position[i] - train_length << "\t" << instant_train_power_consumption[i] / 1000 << "\t" <<

						BX[i] << "\t" << instant_train_energy_consumption[i] * 0.27778 << "\t" << instant_block_section_occupied[i] <<

						// Bs.
						"\n";
				}
			}
		} else { // If instead the train runs on a reversed route then subtract the instant_spatial_position[i] from the Total Length of the route
			// Written positions fall on a reversed route, so the tail is the position plus the train length.
			for (const auto& segment : segments) {
				if (!firstSegment)
					trainout << "\n";
				firstSegment = false;
				for (int i = segment.first; i <= segment.last; i++) {
					trainout << i * timestep << "\t" << instant_train_speed[i] << "\t" << train_route[indexOfRoute].OriginalRefReversedRoute - instant_spatial_position[i] << "\t" << train_route[indexOfRoute].OriginalRefReversedRoute - instant_spatial_position[i] + train_length << "\t" << instant_train_power_consumption[i] / 1000 << "\t" << BX[i] << "\t" << instant_train_energy_consumption[i] * 0.27778 << "\t" << instant_block_section_occupied[i] << "\n";
				}
			}
		}
		trainout.close();
	}

	// Class Destructor
	virtual ~Train() {}

	// This function is used to determine the ID of the TDS the train is currently occupying a block section
	virtual string DetermineOccupiedTDS(int timeinstant, bool IsRouteReversed) {
		string Occupied_TDS_ID = "";
		double BeginningNode = -1, FinalNode = -1; // This are the Beginning and End Node of the TDS
		// if the route of the train is not reversed
		if (IsRouteReversed == 0) {
			if (Bs.TDS_in_block.size() > 0) {
				for (auto i = Bs.TDS_in_block.begin(); i != Bs.TDS_in_block.end(); i++) {
					// if the TDS is straight and not on a diverging switch
					if ((*i)->node_on_switch.X == -1) {
						BeginningNode = (*i)->start_node.X * 1000;
						FinalNode = (*i)->end_node.X * 1000;
					}
					// if instead the TDS is on a diverging switch
					else {
						// Determine Beginning and final nodes
						if ((*i)->node_on_switch.X >= (*i)->connection_node.X) {
							BeginningNode = (*i)->start_node.X * 1000;
							FinalNode = (*i)->node_on_switch.X * 1000;
						}
						// Otherwise it is valid the following condition
						else {
							BeginningNode = (*i)->node_on_switch.X * 1000;
							FinalNode = (*i)->end_node.X * 1000;
						}
					}

					if ((instant_spatial_position[timeinstant] >= BeginningNode) && (instant_spatial_position[timeinstant] < FinalNode)) {
						Occupied_TDS_ID = (*i)->ID;
						break; // break the for loop over the TDS in the block section
					}
				}
			}
		}
		// if instead the route of the train is reversed
		else {
			// the the TDS nodes should be reversed as well
			if (Bs.TDS_in_block.size() > 0) {
				for (auto i = Bs.TDS_in_block.begin(); i != Bs.TDS_in_block.end(); i++) {
					// if the TDS is straight and not on a diverging switch
					if ((*i)->node_on_switch.X == -1) {
						// Beginning Node is the absolute distance between the end Node of the TDS ( as it is reversed) and the absolute position of the Beginning Node of the reversed block + the relative start Node position of the reversed block
						BeginningNode = (Bs.GeoXBegNode - (*i)->end_node.tdsbGeoCoordX) + Bs.start_node.X * 1000;
						FinalNode = (Bs.GeoXBegNode - (*i)->start_node.tdsbGeoCoordX) + Bs.start_node.X * 1000;
					}

					// if instead the TDS is on a diverging switch
					else {
						// Determine Beginning and final nodes
						if ((*i)->node_on_switch.X >= (*i)->connection_node.X) {
							BeginningNode = (Bs.GeoXBegNode - (*i)->node_on_switch.tdsbGeoCoordX) + Bs.start_node.X * 1000;
							FinalNode = (Bs.GeoXBegNode - (*i)->start_node.tdsbGeoCoordX) + Bs.start_node.X * 1000;
						}
						// Otherwise it is valid the following condition
						else {
							BeginningNode = (Bs.GeoXBegNode - (*i)->end_node.tdsbGeoCoordX) + Bs.start_node.X * 1000;
							FinalNode = (Bs.GeoXBegNode - (*i)->node_on_switch.tdsbGeoCoordX) + Bs.start_node.X * 1000;
						}
					}

					if ((instant_spatial_position[timeinstant] >= BeginningNode) && (instant_spatial_position[timeinstant] < FinalNode)) {
						Occupied_TDS_ID = (*i)->ID;
						break; // break the for loop over the TDS in the block section
					}
				}
			}
		}
		// finally return the ID of teh occupied TDS
		return Occupied_TDS_ID;
	}

	void recordDirectIncidentId(const std::string& incidentId, double timeSeconds,
		double locationMeters) {
		if (incidentId.empty())
			return;
		if (std::find(directIncidentIds.begin(), directIncidentIds.end(), incidentId)
			== directIncidentIds.end())
			directIncidentIds.push_back(incidentId);
		if (firstDirectIncidentTime < 0.0) {
			firstDirectIncidentTime = timeSeconds;
			firstDirectIncidentLocation = locationMeters;
		}
	}

	void recordDirectIncident(const SimulationIncident& incident, double timeSeconds,
		double locationMeters) {
		recordDirectIncidentId(incident.id.empty() ? incident.target : incident.id,
			timeSeconds, locationMeters);
	}

	void recordDirectSignalFailure(const MovementAuthority& authority, double timeSeconds) {
		if (authority.type != "SignalFailure")
			return;
		const std::string prefix = "signal_failure:";
		const std::string& encoded = authority.TrainInfo.trainDescription;
		if (encoded.rfind(prefix, 0) != 0)
			return;
		const std::string incidentId = encoded.substr(prefix.size());
		recordDirectIncidentId(incidentId, timeSeconds, authority.AbsPosEoA);
	}

	double effectiveIncidentSpeedLimit(double candidate, int timestepIndex) {
		const SimulationIncident* incident = Active_Train_Breakdown(trainDescription, timestepIndex);
		const double cap = incident ? incident->reducedSpeedKmh / 3.6 : 0.0;
		if (incident && incident->hasReducedSpeed && cap > 0.0 && std::isfinite(cap)
			&& cap < candidate) {
			recordDirectIncident(*incident, timestepIndex * timestep,
				instant_spatial_position.empty() ? 0.0 : instant_spatial_position[std::max(0, timestepIndex - 1)]);
			return cap;
		}
		return candidate;
	}

	// Function to Simulate the Train Trajectory
	virtual void trajectoryComputationIncludingMovingBlock(int time_seconds, double signalCode1, double signalCode2, double signalCode3) {
		if (OutOfSimulation == 0) {
			if (time_seconds == 0) {
				CanEnter = false;
				instant_train_speed[0] = 0.0;
				instant_spatial_position[0] = Start_Node_X * 1000;
				instant_block_section_occupied[0] = "-";
				instant_train_power_consumption[0] = 0.0;
				instant_train_tractive_effort[0] = 0.0;
				instant_train_energy_consumption[0] = 0.0;
				return;
			}
			// A broken-down train holds its last state for the whole incident
			// window; entrance processing is also suspended, so a train whose
			// window covers its entry time simply enters after the window.
			if (time_seconds > 0 && Incident_Holds_Train(trainDescription, time_seconds)) {
				if (const SimulationIncident* incident = Active_Train_Breakdown(trainDescription, time_seconds))
					recordDirectIncident(*incident, time_seconds * timestep, instant_spatial_position[time_seconds - 1]);
				instant_train_speed[time_seconds] = 0;
				instant_spatial_position[time_seconds] = instant_spatial_position[time_seconds - 1];
				instant_train_power_consumption[time_seconds] = 0;
				instant_train_tractive_effort[time_seconds] = 0;
				train_energy_consumption(time_seconds);
				Eq[time_seconds] = 5;
				return;
			}

			// Determine if the Train can enter the route
			checkEntrance(time_seconds, train_route[indexOfRoute].sequence_of_block_sections.data());
			if ((time_seconds >= departure_time) && (CanEnter == 1)) {
				double V_lim;
				int U;
				U = time_seconds;
				int Previous_Block_Index = 0;
				Arc As; // use local variable instead of Train class member (faster simulation)
				string Occupied_TDS;

				// Determination of the right Block Section
				for (int h = 0; h < train_route[indexOfRoute].N_Block_Sections; h++) {
					if ((instant_spatial_position[time_seconds - 1] < train_route[indexOfRoute].sequence_of_block_sections[h].end_node.X * 1000)
						&& (instant_spatial_position[time_seconds - 1] >= train_route[indexOfRoute].sequence_of_block_sections[h].start_node.X * 1000)) {
						Bs = train_route[indexOfRoute].sequence_of_block_sections[h];
						instant_block_section_occupied[time_seconds] = Bs.ID;

						if (h == 0)
							Previous_Block_Index = h;
						else {
							Previous_Block_Index = h - 1;
							break;
						}
					}
				}

				// Determine the TDS which is currently occupied in the Block section Bs by the nose of the train
				Occupied_TDS = DetermineOccupiedTDS((time_seconds - 1), train_route[indexOfRoute].reversed_direction);

				// Determination of the right running Arc
				for (int j = 0; j < Bs.total_arcs; j++) {
					if ((instant_spatial_position[time_seconds - 1] < Bs.arcs_in_signalling_block_section[j].endNode.X * 1000)
						&& (instant_spatial_position[time_seconds - 1] >= Bs.arcs_in_signalling_block_section[j].startNode.X * 1000))
						As = Bs.arcs_in_signalling_block_section[j];
					// logger.Log("\nkkkk");
					////	logger.Log(to_string(instant_spatial_position[time_seconds - 1]));
				}
				// Determination of the right speed limit to observe
				double Speeds[3];
				Speeds[0] = max_train_speed;
				Speeds[1] = As.speedLimit;
				Speeds[2] = As.signalSpeedLimit;
				V_lim = Speeds[0];
				for (int k = 0; k < 3; k++) {
					if (Speeds[k] <= V_lim)
						V_lim = Speeds[k];
				}
				V_lim = effectiveIncidentSpeedLimit(V_lim, time_seconds);

				// Imposing Station Nodes via cached indices
				for (int s = 0; s < numStations; s++) {
					int h = stationBlockSection[s];
					int k = stationArc[s];
					if (h >= 0 && k >= 0) {
						auto& Arc = train_route[indexOfRoute].sequence_of_block_sections[h].arcs_in_signalling_block_section[k];
						if (Arc.endNode.X * 1000 >= instant_spatial_position[time_seconds - 1]) {
							Arc.endNode.station = 1;
							Arc.speedInBraking = 0;
						}
					}
				}

				// Determination of the Movement Authority that has been previouly respected by the train (especially if it stopped at a given EoA)
				// determining Last_MA_StoppedAt for the train
				if ((instant_train_speed[time_seconds - 1] == 0)
					&& (this->BrakingForEoA == 1)
					&& (IsTrainStoppedForEoA == 0)
					&& (this->Last_Received_MA.RelativePosEoA - instant_spatial_position[time_seconds - 1] >= 0)
					&& (this->Last_Received_MA.RelativePosEoA - instant_spatial_position[time_seconds - 1] < 4)) {

					Last_MA_StoppedAt = Last_Received_MA;
					IsTrainStoppedForEoA = true;
					recordDirectSignalFailure(Last_MA_StoppedAt, time_seconds * timestep);
				}

				checkIfMovementAuthorityStillValid(instant_train_speed[time_seconds - 1], instant_spatial_position[time_seconds - 1], Bs);

				leaveFollowingModeWhenLeaderIsGone(time_seconds);

				const double head = instant_spatial_position[time_seconds - 1];
				const double speed = instant_train_speed[time_seconds - 1];
				// Keep a reached speed target until its boundary, but only while it is still present.
				if (Vob[time_seconds - 1] > 0 && speed <= Vob[time_seconds - 1] && head < Xob[time_seconds - 1])
					for (const Section& section : train_route[indexOfRoute].sequence_of_block_sections)
						for (int arc = 0; arc < section.total_arcs; ++arc)
							if (section.arcs_in_signalling_block_section[arc].endNode.X * 1000 == Xob[time_seconds - 1]
								&& section.arcs_in_signalling_block_section[arc].speedInBraking == Vob[time_seconds - 1])
								V_lim = std::min(V_lim, Vob[time_seconds - 1]);
				const bool accelerating = speed < V_lim;
				double candidateSpeed = speed, candidateHead = head + speed * timestep;
				if (accelerating) {
					// This part of the code below instead has been updated to consider the train crusing (and not accelerating like before) is the Traction Surplus with respect to the motion resistances is null. In other words the train is now considered crusing when traction and resistance forces are equal.
					double Traction_Surplus = 0;
					Traction_Surplus = tractiveEffort(instant_train_speed[time_seconds - 1]) - total_train_resistances(instant_train_speed[time_seconds - 1], As.gradient, As.curvature);

					// Compute instant speed
					candidateSpeed = instant_train_speed[time_seconds - 1] + (Traction_Surplus) / (total_train_mass * massFactor) * timestep;

					if (candidateSpeed < 0)
						candidateSpeed = 0;

					if (candidateSpeed > V_lim)
						candidateSpeed = V_lim;

					double Scruis = 0;
					Scruis = instant_spatial_position[time_seconds - 1] + candidateSpeed * timestep;

					// Compute spatial position of the train at current time based on Traction Surplus sign (i.e. whether it is 0, negative or positive)
					if (Traction_Surplus == 0) {
						candidateHead = Scruis;
					} else {
						candidateHead = instant_spatial_position[time_seconds - 1]
							+ ((total_train_mass * massFactor) / (Traction_Surplus)) * instant_train_speed[time_seconds - 1]
								* (candidateSpeed - instant_train_speed[time_seconds - 1]);

						if (candidateHead < Scruis) {
							candidateHead = instant_spatial_position[time_seconds - 1] + instant_train_speed[time_seconds - 1] * timestep + 0.5 * ((candidateSpeed - instant_train_speed[time_seconds - 1]) / timestep) * pow(timestep, 2);
						}
					}
				}
				bool continueCached;
				const double Braking_Distance = europeanVitalComputerWithListsImproved(head, std::max(speed, candidateSpeed),
					time_seconds - 1, train_route[indexOfRoute].sequence_of_block_sections.data(), train_route[indexOfRoute].N_Block_Sections, continueCached);
				const bool mustBrake = continueCached || speed > V_lim || !isShortOfBrakingPoint(head, Braking_Distance)
					|| (candidateSpeed > Vobmin && candidateHead >= Braking_Distance);
				// Check only non-braking candidates; an active plan keeps its own force-bounded step.
				const bool infeasibleCandidate = !mustBrake && speed > 0 && candidateSpeed < brakingCurveSpeed(speed, As);
				// Acceleration phase
				if (!mustBrake && !infeasibleCandidate && accelerating) {
					instant_train_speed[time_seconds] = candidateSpeed;
					instant_spatial_position[time_seconds] = candidateHead;

					Eq[time_seconds] = 1;
					instant_train_tractive_effort[time_seconds] = tractiveEffort(instant_train_speed[time_seconds - 1]);
					instant_train_power_consumption[time_seconds] = instant_train_tractive_effort[time_seconds] * instant_train_speed[time_seconds - 1];
					train_energy_consumption(time_seconds);
					temp = time_seconds;
					stop = time_seconds;
					BX[time_seconds] = Braking_Distance;
					Xob[time_seconds] = Xobmin;
					Vob[time_seconds] = Vobmin;
					counter = 0;
					GradientExceptionInBraking = false;
				}

				/*//Cruising Phase
				else if((instant_spatial_position[i-1]<Braking_Distance)&&(fabs((V_lim-instant_train_speed[i-1]))<2.78*timestep)){
				instant_train_speed[i]=V_lim;    if (instant_train_speed[i]<0)instant_train_speed[i]=0;
				instant_spatial_position[i]=instant_spatial_position[i-1]+instant_train_speed[i-1]*timestep;
				instant_train_power_consumption[i]=total_train_resistances(instant_train_speed[i-1],As.gradient,As.curvature)*instant_train_speed[i-1];    Eq[i]=2;
				temp=i; stop=i; BX[i]=Braking_Distance;Xob[i]=Xobmin; Vob[i]=Vobmin; counter=0;}*/

				// Braking Phase
				else if ((mustBrake || infeasibleCandidate)
					&& (instant_spatial_position[time_seconds - 1] < train_route[indexOfRoute].x_of_end_node * 1000)
					&& (speed > 0 || (speed == 0 && mustBrake))) {

					if (speed == 0 || infeasibleCandidate || (speed > V_lim && isShortOfBrakingPoint(head, Braking_Distance))) {
						fullBrakingStep(time_seconds, As);
						counter = 0;
					} else
						brakingStep(time_seconds, As, train_route[indexOfRoute].sequence_of_block_sections.data(), train_route[indexOfRoute].N_Block_Sections);

					// if two braking curves are both restrictive
					/*if ((Xobj!=Xobmin)||(Vobj!=Vobmin)||((Xobj!=Xobmin)&&(Vobj!=Vobmin))){counter=1;
					Xobj=Xobmin;   Vobj=Vobmin;   Eq[i]=53;
					DrawBrakingCurve(instant_train_speed[i-1],Vobj,Xobj,signalling_block_sections,Blocks);
					for (int t=0;t<BrakStep;t++){
					if((Vbrak[t]<instant_train_speed[i-1])&&(Sbrak[t]>instant_spatial_position[i-1])){ brakingPoint=t; break;}}
					instant_train_speed[i]=Vbrak[brakingPoint+counter-1];    if (instant_train_speed[i]<0)instant_train_speed[i]=0;
					instant_spatial_position[i]=Sbrak[brakingPoint+counter-1];}*/

					if (Eq[time_seconds] == 54) {
						instant_train_tractive_effort[time_seconds] =
							-(brakingEffort((time_seconds - temp - 1) * timestep)
								+ gradient_resistances(As.gradient)
								+ curvature_resistances(As.curvature)
								- total_train_resistances(instant_train_speed[time_seconds - 1], As.gradient, As.curvature));
					} else {
						instant_train_tractive_effort[time_seconds] = total_train_mass * massFactor * (instant_train_speed[time_seconds] - speed) / timestep
							+ total_train_resistances(speed, As.gradient, As.curvature);
					}
					instant_train_power_consumption[time_seconds] =
						instant_train_tractive_effort[time_seconds] * instant_train_speed[time_seconds - 1]; /*Eq[i]=3;*/
					train_energy_consumption(time_seconds);
					// A fallback from rest must not restart an active station's dwell clock.
					if (speed > 0)
						stop = time_seconds;
					BX[time_seconds] = Braking_Distance;
					Xob[time_seconds] = Xobmin;
					Vob[time_seconds] = Vobmin;
				}

				/*//Brake if the train enters next Arc with a speed higher than the Arc speed limit
				else if ((instant_train_speed[i-1]>V_lim)){
				instant_train_speed[i]=instant_train_speed[i-1]+(-brakingEffort((i-temp-1)*timestep)-(gradient_resistances(As.gradient)+curvature_resistances(As.curvature)))/(total_train_mass*massFactor)*timestep;     if (instant_train_speed[i]<0)instant_train_speed[i]=0;
				instant_spatial_position[i]=instant_spatial_position[i-1]+((total_train_mass*massFactor)/(-brakingEffort((i-temp-1)*timestep)-(gradient_resistances(As.gradient)+curvature_resistances(As.curvature))))*instant_train_speed[i-1]*(instant_train_speed[i]-instant_train_speed[i-1]);
				instant_train_power_consumption[i]=-(brakingEffort((i-temp-1)*timestep)+gradient_resistances(As.gradient)+curvature_resistances(As.curvature)-total_train_resistances(instant_train_speed[i-1],As.gradient,As.curvature))*instant_train_speed[i-1];   Eq[i]=4;
				BX[i]=Braking_Distance;Xob[i]=Xobmin; Vob[i]=Vobmin; stop=i; counter=0;}*/

				// Cruising Phase
				else if (!mustBrake && !accelerating && speed > 0) {
					instant_train_speed[time_seconds] = candidateSpeed;
					instant_spatial_position[time_seconds] = candidateHead;
					const double cruisingEffort = total_train_resistances(speed, As.gradient, As.curvature);
					instant_train_tractive_effort[time_seconds] = cruisingEffort;
					instant_train_power_consumption[time_seconds] = cruisingEffort * speed;
					Eq[time_seconds] = 12;
					train_energy_consumption(time_seconds);
					temp = time_seconds;
					stop = time_seconds;
					BX[time_seconds] = Braking_Distance;
					Xob[time_seconds] = Xobmin;
					Vob[time_seconds] = Vobmin;
					counter = 0;
					GradientExceptionInBraking = false;
				}

				double leaderHead = 0;
				const MovementAuthority* liveLeader = IsTrainCoupling || IsTrainInFollowingMode
					? currentFollowingAuthority(head, leaderHead)
					: nullptr;
				// Determination of Train Coupling phase for Virtual Coupling
				if ((IsTrainCoupling == 1) && liveLeader
					&& liveLeader->TrainInfo.trainDescription == Predicted_MA_To_CoupleAt.TrainInfo.trainDescription
					&& ((IsTrainInFollowingMode == 0) || (IsInUnintentionalDecoupling == 1))
					&& (abs(instant_train_speed[time_seconds] - liveLeader->TrainInfo.TrainSpeed) < 0.278)
					&& (abs(this->Predicted_MA_To_CoupleAt.RelativePosEoA - instant_spatial_position[time_seconds - 1]) <= 30)) {
					// When the condition above occurs then the train is transitioning towards the following mode
					IsInUnintentionalDecoupling = false;
					IsTrainCoupling = false;														   // So we set the ending of the Coupling transition period
					IsTrainInFollowingMode = true;													   // And the activation of the following mode
					LeadingTrainInFollowingMode = Predicted_MA_To_CoupleAt.TrainInfo.trainDescription; // Setting the trainDescription of the train which is leading during Virtual Coupling
																									   // Sending a message that train is in following mode
					EG_LOG(SimEvents, Info) << "At time instant " << time_seconds << " train " << trainDescription << " is virtually coupled to train " << LeadingTrainInFollowingMode << "\n";

					//-------------------------------
					// GUI notification
					VCmsgTimestep.push_back(time_seconds);
					VCmsgTrain.push_back(trainDescription);
					VCmsgText.push_back("Train is now virtually\ncoupled to the leader");
					//-------------------------------
				}

				// Determination of Train Uncoupling from the leader when it is in following mode and the route diverges from the leader's
				if ((IsTrainInFollowingMode == 1) && (IsTrainDecoupling == 1)) {
					// When the train is decoupling it can be considered totally uncoupled and not anymore in following mode when the tale of the leading train has fully crossed the splitting infrastructure element and the corresponding MA is communicated with type="TrainUncoupled"
					if ((Predicted_MA_To_DecoupleAt.TrainInfo.trainDescription != "None")
						&& (Predicted_MA_To_DecoupleAt.TrainInfo.trainDescription == LeadingTrainInFollowingMode)
						&& (Predicted_MA_To_DecoupleAt.type == "TrainUncoupled")) {

						EG_LOG(SimEvents, Info) << "At time instant " << time_seconds << " train " << trainDescription << " intentionally decoupled from train " << LeadingTrainInFollowingMode << " because of diverging routes\n";

						//-------------------------------
						// GUI notification
						VCmsgTimestep.push_back(time_seconds);
						VCmsgTrain.push_back(trainDescription);
						VCmsgText.push_back("Train is now decoupled");
						//-------------------------------

						MovementAuthority ResetPredictedMAToDecoupleAt;
						IsTrainInFollowingMode = false;
						LeadingTrainInFollowingMode = "None";
						IsTrainDecoupling = false;
						Predicted_MA_To_DecoupleAt = ResetPredictedMAToDecoupleAt;
						IsInUnintentionalDecoupling = false;
					}
				}

				/*******************  Virtual Coupling Following mode equations  ***************************************************************************************************************************/

				if ((IsTrainInFollowingMode == 1) && (IsTrainDecoupling == 0) && (IsInUnintentionalDecoupling == 0)) {
					CounterFollowingMode++; // This sets the number of time steps that the train is in following mode
					// if the train has outdistanced for more than 30 meters from the EoA of the train ahead, then interrupt the following mode and let the train to go free
					if (liveLeader && liveLeader->TrainInfo.trainDescription == LeadingTrainInFollowingMode
						&& leaderHead - head > 30) {
						IsInUnintentionalDecoupling = true; // Originally I had IsTrainInFollowingMode = false;
						CounterFollowingMode = 0;			// set also the CounterFollowingMode to 0 again

						EG_LOG(SimEvents, Info) << "At time instant " << time_seconds << " train " << trainDescription << " is unintentionally decoupling from train " << LeadingTrainInFollowingMode << " because it could not reach the same speeds\n";

						//-------------------------------
						// GUI notification
						VCmsgTimestep.push_back(time_seconds);
						VCmsgTrain.push_back(trainDescription);
						VCmsgText.push_back("Train is now unintentionally\ndecoupling from the leader");
						//-------------------------------
					}

					// The first coupling step keeps the normal result. Later steps are proposals, not overrides.
					const bool stationaryHold = speed == 0 && (V_lim == 0 || (As.endNode.X * 1000 - head < 4 && (As.endNode.station || As.endNode.respectOrder || !strcmp(Bs.state, "red"))));
					if (CounterFollowingMode > 1 && liveLeader && liveLeader->TrainInfo.trainDescription == LeadingTrainInFollowingMode
						&& !mustBrake && !infeasibleCandidate && !IsTrainStoppedForEoA && !StoppedForServiceStop && !stationaryHold
						&& std::isfinite(liveLeader->TrainInfo.TrainSpeed) && std::isfinite(liveLeader->TrainInfo.Acceleration)) {
						const double mass = total_train_mass * massFactor;
						const double resistance = total_train_resistances(speed, As.gradient, As.curvature);
						const double minimum = brakingCurveSpeed(speed, As);
						const double maximum = std::max(0.0, speed + (tractiveEffort(speed) - resistance) / mass * timestep);
						const double acceleration = std::max((minimum - speed) / timestep,
							std::min((maximum - speed) / timestep, liveLeader->TrainInfo.Acceleration));
						double proposedSpeed = (acceleration < 0 ? liveLeader->TrainInfo.TrainSpeed : speed) + acceleration * timestep;
						if (acceleration == 0 && liveLeader->TrainInfo.TrainSpeed == 0)
							proposedSpeed = 0;
						// Bound the final leader-relative intent by this train's own actual-start force interval.
						proposedSpeed = std::min(V_lim, std::max(minimum, std::min(maximum, std::max(0.0, proposedSpeed))));
						const double proposedHead = head + speed * timestep;
						if (std::isfinite(proposedHead) && std::isfinite(proposedSpeed) && proposedHead >= head && proposedSpeed >= 0
							&& proposedSpeed >= minimum && proposedSpeed <= maximum && proposedSpeed <= V_lim
							&& proposedHead <= instant_spatial_position[time_seconds] && proposedSpeed <= instant_train_speed[time_seconds]
							&& isShortOfBrakingPoint(proposedHead, Braking_Distance)) {
							instant_spatial_position[time_seconds] = proposedHead;
							instant_train_speed[time_seconds] = proposedSpeed;
							instant_train_tractive_effort[time_seconds] = mass * (proposedSpeed - speed) / timestep + resistance;
							instant_train_power_consumption[time_seconds] = instant_train_tractive_effort[time_seconds] * speed;
							train_energy_consumption(time_seconds);
							temp = time_seconds;
							stop = time_seconds;
							counter = 0;
							Eq[time_seconds] = 1002;
						}
					}
				}

				/**************************************************************************************************************************/

				// Release of signalling system when the train exits simulation
				if (instant_spatial_position[time_seconds - 1] >= train_route[indexOfRoute].x_of_end_node * 1000) {
					End_Time = time_seconds - 1;
					if (destinationTerminationRequested)
						destinationTerminated = true;
					instant_train_speed[time_seconds] = 0;
					instant_spatial_position[time_seconds] = -9999;
					instant_train_power_consumption[time_seconds] = -9999;
					instant_train_tractive_effort[time_seconds] = -9999;
					Xobmin = train_route[indexOfRoute].x_of_end_node * 1000;
					Vobmin = train_route[indexOfRoute].sequence_of_block_sections[train_route[indexOfRoute].N_Block_Sections - 1].arcs_in_signalling_block_section[train_route[indexOfRoute].sequence_of_block_sections[train_route[indexOfRoute].N_Block_Sections - 1].total_arcs - 1].speedInBraking; /*This is the speedInBraking of the last Arc of the Route*/
					relLastSectionMixedSignalling(train_route[indexOfRoute].sequence_of_block_sections[train_route[indexOfRoute].N_Block_Sections - 1].ID);
					OutOfSimulation = true;
				}
				/*else if (instant_spatial_position[i-1]<0){instant_train_speed[i]=0; instant_spatial_position[i]=-9999; instant_train_power_consumption[i]=-9999; }*/

				/**************************************************************************************************************************/

				// Standing Train Conditions
				if (instant_train_speed[time_seconds - 1] == 0) {

					// A replacement authority's approved stationary step also constrains node handling.
					const bool authorityHold = IsTrainStoppedForEoA || (mustBrake && BrakingForEoA && instant_train_speed[time_seconds] == 0 && instant_spatial_position[time_seconds] == head);

					// Stopping at a station for an estabilished dwell time
					if (((As.endNode.X * 1000 - instant_spatial_position[time_seconds - 1]) < 4) && (As.endNode.station == 1)) {
						this->StoppedForServiceStop = true; // Stating that the train is stopping at a station to perform a service stop
						Eq[time_seconds] = 5;
						double stoptime = 0;
						double dep_time = 0;
						counter = 0;
						for (int s = 0; s < numStations; s++) {
							if (Stations[s].stationName == As.endNode.stationName) {
								this->CurrentServiceStop = Stations[s].stationName; // Setting the name of the station the train is currently stopping at
								this->CurrentServiceStopPlatform = Stations[s].stationPlatformId;
								recordCurrentServiceStopArrival(time_seconds);

								EG_LOG(SimStep, Info) << "At time " << time_seconds << " Train" << this->trainDescription << " is stopping at " << this->CurrentServiceStop << " at " << this->CurrentServiceStopPlatform << "\n";

								stoptime = Stations[s].StopTime;
								dep_time = ScheduledDepartures[s];
								recordCurrentServiceStopArrival(time_seconds);
								if (Stations[s].StepStopped == 0)
									stop = time_seconds;
								Stations[s].StepStopped++;
								if (Stations[s].StepStopped > Stations[s].StopTime)
									stoptime = 0;
							}
						}


						// Check if in this Station the train has also to respect a given OL Order
						if (As.endNode.respectOrder == 1) {
							bool Condition_To_Stop_Train = Conditions_To_Stop_Train_For_OL_Order(As.endNode.respectOrder, As.endNode.indexOrderList, Bs);
							// The train can leave the station only if the previous train has passed, it has satisfied the min dwell time and the signal aspect is non restrictive and there is not condition to stop the train becasue of an ETCS 3 EoA
							if ((Condition_To_Stop_Train == 0) && ((time_seconds - stop) > stoptime) && (time_seconds > dep_time) && (authorityHold == 0)) {
								instant_train_speed[time_seconds] = 0.0001;
								instant_spatial_position[time_seconds] = As.endNode.X * 1000 + 0.0001;
								this->StoppedForServiceStop = false; // Stating that the train is no longer stoppoed at a service station
								this->CurrentServiceStop = "None";	 // This states that the train is no longer stopping at the service station
							}
							// Otherwise the train has to remain stopped at the station
							else {
								instant_train_speed[time_seconds] = 0;
								instant_spatial_position[time_seconds] = authorityHold ? head : As.endNode.X * 1000 - 0.0001;
							}
						}
						// If the station Node does not have to respect a specific train order, the train has to be stopped at the station for the dwell time and can depart only after that ensuring that it does not have to respect any ETCS 3 EoA at that station
						else {
							// A platform at the end of a block section is also the stopping point of that block's signal: the train waits there while the signal is red.
							const bool signalAtPlatformIsRed = Bs.total_arcs > 0
								&& std::fabs(Bs.arcs_in_signalling_block_section[Bs.total_arcs - 1].endNode.X - As.endNode.X) < 1e-9
								&& strcmp(Bs.state, "red") == 0;
							if (((time_seconds - stop) <= stoptime) || (time_seconds <= dep_time) || (authorityHold == 1) || signalAtPlatformIsRed) {
								instant_train_speed[time_seconds] = 0;
								instant_spatial_position[time_seconds] = authorityHold ? head : As.endNode.X * 1000 - 0.0001;
							} else {
								instant_train_speed[time_seconds] = 0.0001;
								instant_spatial_position[time_seconds] = As.endNode.X * 1000 + 0.0001;
								this->StoppedForServiceStop = false; // Stating that the train is no longer stoppoed at a service station
								this->CurrentServiceStop = "None";	 // This states that the train is no longer stopping at the service station
								this->CurrentServiceStopPlatform = "None";
								BrakingForEoA = false; // setting the variable Braking for EoA to false since the train is starting to move again
							}
						}
						instant_train_power_consumption[time_seconds] = 0;
						instant_train_tractive_effort[time_seconds] = 0;
						train_energy_consumption(time_seconds);
					}

					// Stopping at a checkpoint Node where a certain train order must be respected
					else if ((As.endNode.X * 1000 - instant_spatial_position[time_seconds - 1] < 4) && (As.endNode.respectOrder == 1)) {
						counter = 0;
						bool Cond_To_Stop_Train = Conditions_To_Stop_Train_For_OL_Order(As.endNode.respectOrder, As.endNode.indexOrderList, Bs);


						if ((Cond_To_Stop_Train == 0) && (authorityHold == 0)) { // the checkpoint Node can coincide with an ETCS3 EoA, that is why the train can depart from this Node only after having verified that the checkpoint does not coincide with an ETCS3 EoA or that the EoA has been deleted
							Eq[time_seconds] = 532;
							instant_train_speed[time_seconds] = 0.0001;
							instant_spatial_position[time_seconds] = As.endNode.X * 1000 + 0.0001;
							BrakingForEoA = false; // setting back to false BrakingForEoa since the train starts to move again
						}
						// otherwise it has to be stopped
						else {
							Eq[time_seconds] = 534;
							instant_train_speed[time_seconds] = 0;
							instant_spatial_position[time_seconds] = authorityHold ? head : As.endNode.X * 1000 - 0.0001;
						}

						instant_train_power_consumption[time_seconds] = 0;
						instant_train_tractive_effort[time_seconds] = 0;
						train_energy_consumption(time_seconds);
					}

					// Stopping at a Red Signal
					else if (As.endNode.X * 1000 - instant_spatial_position[time_seconds - 1] < 4) {
						Eq[time_seconds] = 6;

						// also a signal can coincide with an ETCS3 EoA, that is why a train must stop at the signal also if this not red but and ETCS 3 EoA exists
						if ((!strcmp(Bs.state, "red")) || (authorityHold == 1)) {
							instant_spatial_position[time_seconds] = authorityHold ? head : As.endNode.X * 1000 - 0.0001;
							instant_train_speed[time_seconds] = 0;
						} else {
							instant_spatial_position[time_seconds] = As.endNode.X * 1000 + 0.0001;
							instant_train_speed[time_seconds] = 0.0001;
							BrakingForEoA = false; // setting BrakingForEoA to false since the train starts moving again
						}
						instant_train_power_consumption[time_seconds] = 0;
						instant_train_tractive_effort[time_seconds] = 0;
						train_energy_consumption(time_seconds);
						counter = 0;
					}
					// if noone of the previous conditions is verified it means that the train is stopped at an ETCS3 EoA
					else if (this->IsTrainStoppedForEoA == 1) {
						instant_train_speed[time_seconds] = 0;
						instant_spatial_position[time_seconds] = head;
						Eq[time_seconds] = 323;
						instant_train_power_consumption[time_seconds] = 0;
						instant_train_tractive_effort[time_seconds] = 0;
						train_energy_consumption(time_seconds);
						counter = 0;
					}

					// if the speed limit on the Arc is 0: this case happens only when the BACC is implemented
					if (V_lim == 0) {
						counter = 0;
						Eq[time_seconds] = 958;
						instant_train_speed[time_seconds] = 0;
						instant_spatial_position[time_seconds] = instant_spatial_position[time_seconds - 1];
						instant_train_power_consumption[time_seconds] = 0;
						instant_train_tractive_effort[time_seconds] = 0;
						train_energy_consumption(time_seconds);
					}
					GradientExceptionInBraking = false;
				}

				// Update the status of Infrastructure elements on the route of the train
				if (time_seconds > 0) {
					UpdateInfrastructureElementStatus(
						train_route[indexOfRoute].InfrastructureElements, time_seconds,
						instant_spatial_position[time_seconds - 1],
						instant_spatial_position[time_seconds],
						instant_train_speed[time_seconds - 1],
						trainDescription);
				}

				// if the train has just entered the track therefore its position instant_spatial_position[i] is higher than 1 meter and lower than 3 meters VirtualQ[ID-1] must be updated to ID
				Update_LastEnteredTrain_For_All_OL_Improved(time_seconds, train_route[indexOfRoute].sequence_of_block_sections[Previous_Block_Index]);
			}
			/***********************************************************************************************************************/
			else {
				instant_train_speed[time_seconds] = 0.0;
				instant_spatial_position[time_seconds] = Start_Node_X * 1000;
				instant_block_section_occupied[time_seconds] = "-";
				instant_train_power_consumption[time_seconds] = 0.0;
				instant_train_tractive_effort[time_seconds] = 0.0;
			}
			// cout << instant_spatial_position[i] << " " << As.gradient << " " << As.curvature << "\n";
		} else {
			instant_block_section_occupied[time_seconds] = "-";
		}
	}

	// Function to compute the arrival and departure of a train from a location with a specific position expressed in [m]
	void computeArrivalAndDepartureAtLocation(double LocationPosition, TrainEvent& PassingPoint);

	// Function to compute the passing time of a train over each timetable point (i.e. stations, junctions and or relevant signals)
	void ComputeTimetablingPoints();

	// Function to Determine which Block Section is Occupied by the train
	virtual void Det_Section_Occupied_By_Train(int i, Section* BS, int Blocks);

	// Function to Report Train Position to RBC (the ETCS3 Safety Margin must be given in m)
	void ReportPositionToRBC(int i, Section* BS, int N_BS, double ETCS3SafetyMargin);

	// Function to Compute Blocking Times for a single Block Section. This Function allows us to compute blocking time in area with mixed signalling system
	/**signalling_block_sections is the Block Section in the route of train, IndexBS is instead the index of that Block Section in the Route*/
	void ComputeBlockingTimeForSingleLocation(int IndexCurrentBS, string SignallingType, int EntryTime, double SetupTime, double ReleaseTime, double SightReacTime, double AbsoluteRTSupplement, double PercentageRTSupplement);

	double ComputeLastOccupationTime_real_time(int t, std::string CurrentBS, int EntryTime);

	// Function to Compute the Blocking Times in ETCS level 3 (EntryTime is the time the train actually enters the network in the simulation)
	void ComputeBlockTime_ETCSLevel_3_ForSection(int IndexCurrentBS, int EntryTime, double SetupTime, double ReleaseTime, double SightReacTime, double SafetyMargin, double AbsoluteRTSupplement, double PercentRTSupplement);

	// Function to Compute the Blocking Times in Virtual Coupling (EntryTime is the time the train actually enters the network in the simulation)
	/**This function uses a principle of max capacity where trains travel at a minimum safety distance from each other over the entire shared route, splitting apart by an absolute braking distance only at those junctions where they diverge or converge*/
	void ComputeBlockTime_ETCSLevel4_ForSection_MaxCapacity_Improved(int IndexCurrentBS, int EntryTime, double SetupTime, double ReleaseTime, double SightReacTime, double SafetyMargin, double AbsoluteRTSupplement, double PercentRTSupplement);

	// Function to Connect the Blocking Times of edges of diverging switches for ETCS Level 3 and Virtual Coupling. In this case the diverging switch needs to be considered as a single block section, that is why blocking times of connected edges need to be accordingly modified
	void ConnectBlockTimesOfSwitchesForTrainCentricSignalling();

	// Function to Compute the Blocking times in mixed signalling areas
	void ComputeBlockingTimesInMixedSignallingAreas(double SetupTime, double ReleaseTime, double SightReacTime, double SafetyMargin, double AbsRTSupp, double PercRTSupp);

	// Function to evaluate the time instant in which for the first time the train Starts to accelerate (It's the time corresponding to the first non-zero speed value)
	double Det_Run_Start_Time();

	// The timetable point of a stop of this train, or nullptr if there is none. A station served more than
	// once has one timetable point per call, in the order of the stops.
	const TrainEvent* timetablePointOfStop(int stopIndex) const;

	// Function to setup the station arrivals of the trains from the arrival at their timetable points.
	// That is the arrival the timetable results report, so the delay statistics use the same event. A stop
	// whose timetable point has no arrival (the train did not get there) has no arrival. The positions
	// recorded during the run only serve a stop that has no timetable point at all.
	void Determine_Actual_Station_Arrivals();

	// Function compute Arrival delays at stations for a train
	void computeArrivalDelaysAtStations();

	// Function to calculate train delays taking into account both the positive and negative (arrival ahead the schedule) delays
	void Compute_Pos_And_Neg_Arrival_Delays_At_Stations();

	void recordCurrentServiceStopArrival(int time_seconds) {
		if (time_seconds < 0 || CurrentServiceStop == "None")
			return;

		for (int s = 0; s < numStations; s++) {
			if (Stations[s].stationName == CurrentServiceStop && StationArrivals[s] < 0) {
				StationArrivals[s] = time_seconds * timestep;
				StationArrivalNames[s] = CurrentServiceStop;
				break;
			}
		}
	}

	bool stationIsOnRoute(int stationIndex, const vector<string>& allowedBlockIds = {}) const;

	double stationRoutePositionMeters(int stationIndex) const;

	void recordStationPassagesAtTime(int time_seconds);

	string stationNameForArrivalStats(int stationIndex) const {
		if (stationIndex < 0 || stationIndex >= numStations)
			return "None";
		if (StationArrivalNames[stationIndex] != "None")
			return StationArrivalNames[stationIndex];
		return Stations[stationIndex].stationName;
	}

	// check train arrival/departure at/from destination/origin
	void checkTrainArrDep(int trainIdx, int t);

	// print train service path diagram to file
	void printTrainServicePathDiagram(std::string FolderName, int nextServiceRouteID);

	// print train arrival at terminal station (append to file)
	void printTrainArrDepMsg(std::string stationName, std::string msgType, int trainIdx, int t, std::string FolderName);

	// computes train X position (works with routes acrossing different regions)
	/** wagon = 0 by default to return head position*/
	double trainXPosition(int t, int wagon = 0);

	// instantaneous speed in km/h at GUI time t, mirroring trainXPosition index mapping
	double speedKmhAt(int t) const {
		if (t < 0 || t >= static_cast<int>(instant_train_speed.size()))
			return 0.0;
		return instant_train_speed[t] * 3.6; // m/s to km/h
	}

	// position in km at GUI time t, mirroring trainXPosition index mapping
	double positionKmAt(int t) const {
		if (t < 0 || t >= static_cast<int>(instant_spatial_position.size()))
			return 0.0;
		return instant_spatial_position[t] / 1000.0;
	}

	// number of trajectory samples available
	int trajectorySize() const {
		return static_cast<int>(instant_train_speed.size());
	}

	void recordEarliestActiveTrajectoryIndex(int index) {
		earliestActiveTrajectoryIndex = recordEarliestTrajectoryIndex(earliestActiveTrajectoryIndex, index, CanEnter);
	}

	// set vector sizes with length of simulation from user input
	void setTrainVectorSizesFromInput(int vec_size);
};

class Regional : public Train {

public:
	// Regional class Constructor
	Regional();

	// Regional Destructor
	~Regional() {
		/*PrintTrajectory();*/
		// cout << "\nTrain " << this->ID << " Destroyed";
	}

	// moved PrintTrajectory to simulation code because here destructor automatically deletes vectors before print function

	// Overloading = Operator  (Useful to create more trains belonging to the same line)
	Regional& operator=(const Regional& ob2) {
		number_of_wagons = ob2.number_of_wagons;
		MAX_OnBoard_Passengers = trainPassengerCapacity(number_of_wagons);
		massFactor = ob2.massFactor;
		mass_of_traction_unit = ob2.mass_of_traction_unit;
		mass_of_a_wagon = ob2.mass_of_a_wagon;
		total_train_mass = ob2.total_train_mass;
		max_train_decelaration = ob2.max_train_decelaration;
		max_train_speed = ob2.max_train_speed;
		g = ob2.g;
		frontal_wagon_area = ob2.frontal_wagon_area;
		massPerWagonAxle = ob2.massPerWagonAxle;
		Jerk = ob2.Jerk;
		train_length = ob2.train_length;
		operatingCode = ob2.operatingCode;
		serviceId = ob2.serviceId;
		serviceOccurrence = ob2.serviceOccurrence;
		servicePerformancePercent = ob2.servicePerformancePercent;
		hasConfiguredMaximumSpeed = ob2.hasConfiguredMaximumSpeed;
		configuredMaximumSpeedKmh = ob2.configuredMaximumSpeedKmh;
		compositionMaximumSpeedMs = ob2.compositionMaximumSpeedMs;
		appliedMaximumSpeedMs = ob2.appliedMaximumSpeedMs;
		appliedMaximumSpeedKmh = ob2.appliedMaximumSpeedKmh;
		directIncidentIds = ob2.directIncidentIds;
		firstDirectIncidentTime = ob2.firstDirectIncidentTime;
		firstDirectIncidentLocation = ob2.firstDirectIncidentLocation;
		destinationTerminationRequested = ob2.destinationTerminationRequested;
		destinationTerminated = ob2.destinationTerminated;
		return *this;
	}
};

// ComputeEnergyConsumptionForAllTrains takes Train* and is called with regional_train.data(),
// so both types must have the same size.
static_assert(sizeof(Regional) == sizeof(Train), "Regional must not add members to Train");

// One element per train of the loaded scene. The storage is created once per build and does not
// grow while the trains are live.
extern std::vector<Regional> regional_train;

// Function to Determine for each Route the Block Sections that are occupied by trains (This Function Fill in the list BlocksOccupied)
void Occupy_Block_Sections_Of_Route(int i);

// Function to report the position of the trains in ETCS Level 3
void ReportAllTrainPositionsToRBC(int i, double ETCS3SafetyMargin);

// Function to Print out the blocking times of all the Trains
void PrintTrainBlockingTimes(string MainFolder);

// Function to Print out the blocking times of all the Trains
void PrintTimetablePoints(string MainFolder);

// Function to compute Blocking Times of trains in mixed signalling areas
void ComputeBlockingTimesInMixedSignallingForAllTrains(double SetupTime, double ReleaseTime, double SightReacTime, double SafetyMargin, string OutputFolder, double AbsRTSupplement, double PercRTSupplement);

// function to protect all station areas
void protectStationAreas(int i);

#endif
