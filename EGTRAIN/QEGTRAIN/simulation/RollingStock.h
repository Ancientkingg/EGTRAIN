#ifndef Rolling_Stock_hpp
#define Rolling_Stock_hpp

// The member functions of the classes in this header are defined in the RollingStock*.cpp files in this folder. Only these short
// bodies stay inline, with the files that call them:
// - the empty destructors of Train and Regional;
// - BlockingTimeList (RollingStockBlockingTimes.cpp, RollingStock.cpp, MainWindow.cpp, test_characterization.cpp);
// - isShortOfBrakingPoint, isPastStopHoldback and their constants (RollingStockStops.cpp, RollingStockTrajectory.cpp,
//   test_operationsbuilder.cpp);
// - Train::brakingCurveSpeed (RollingStockBraking.cpp, RollingStockTrajectory.cpp, test_operationsbuilder.cpp);
// - Train::obeysAuthority (RollingStockBraking.cpp, RollingStockTrajectory.cpp) and Train::authorityRoutePosition
//   (RollingStockBraking.cpp, RollingStockTrajectory.cpp, test_operationsbuilder.cpp);
// - the trajectory accessors Train::speedKmhAt, positionKmAt, trajectorySize and recordEarliestActiveTrajectoryIndex
//   (DispatchController.cpp, MainWindow.cpp, RollingStockStops.cpp, test_operationsbuilder.cpp).

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
#include "scene/RuntimeLimits.h"
#include "util/Log.h"
#include "util/TrajectoryUtil.h"

// GUI - Virtual Coupling notifications
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <limits>
#include <vector>
extern vector<int>& VCmsgTimestep;
extern vector<string>& VCmsgTrain;
extern vector<string>& VCmsgText;

// -------------


#include <cmath> // std::fabs

extern InitialParameters initial_variables;

// Build rolling stock, timetable, selected-scenario incidents, entrance
// delays, platforms, and passengers from an in-memory canonical scene.
// The function is transactional: errors leave existing operation globals intact.
std::vector<SceneDiagnostic> buildOperationsFromScene(const SceneModel& scene,
	const std::string& selectedScenarioId = {},
	const SceneRunSelection& selectedOccurrences = {});

// Returns the operations state of the run to that of a new run: the trains and their storage, platforms, passengers,
// incidents and statistics.
void prepareNativeOperationsState();

// The reset between scene runs, which is the same as preparing a new run.
void resetNativeOperationsState();

// Passenger capacity of a train: 300 for the traction unit and 300 for each wagon.
int trainPassengerCapacity(double numberOfWagons);

// Occupancy as a ratio of passengers to capacity. An unknown capacity (zero or
// negative) gives 0, so it never counts as congested.
double passengerOccupancyRatio(int passengers, int capacity);


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
	// Writes to stderr and not through EG_LOG: the sink writes std::cout without a flush and
	// the pair can be switched off, so the message could be lost when abort() follows.
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
extern int& numRegions; /*Number of Trains loaded for the simulation*/

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
	std::string entranceDelayStationId; // Station ID (runtime station name) of the first stop whose planned departure the entrance delay shifted; empty when none was applied
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

	virtual double tractiveEffort(double V);

	//  Calculation of Braking Effort affected by Jerk

	virtual double brakingEffort(double T);


	//! Calculation of Traction Unit Resistances due to air viscosity
	//!  (Modified Strahl Formulae_Italian FS Formulation)
	virtual double traction_unit_resistences(double V);

	//! Calculation of Train Wagons Resistances using the Sauthoff Formula
	virtual double wagons_resistances(double V);

	//! Calculation of Train Resistances due to Track Gradient
	virtual double gradient_resistances(double G);

	//! Calculation of Train Resistances due to Track Curvature
	virtual double curvature_resistances(double R);

	//! Calculation of the sum of all Train Resistances
	virtual double total_train_resistances(double V, double G, double R);

	virtual void Initialise_Gibson_Dwell_Time_Parameters(float beta0, float beta1, float beta2, float beta3, float beta4, float beta5, float beta6, float beta7);

	//! Calculation of Train Energy Consumption
	double train_energy_consumption(int t);

	// A completed trajectory can leave a non-finite power sample at End_Time;
	// treat that boundary sample and its aligned effort as zero, matching
	// train_energy_consumption().
	void sanitizeTerminalPowerSample();

	// Function to calculate Total Energy Consumed with and without regenrative braking (the inputs are the Eta of the Substation and the Eta of the regenerative braking)
	void TotalEnergyConsumptionWithAndWithoutRegBraking(double EtaSubst, double EtaRegBrak);

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

	void checkEntrance(int i, Section* BS);

	// Function to compute dwell times based on the interaction with passengers
	// By default the dwell time is computed based on the microscopic dwell time model by Fernandez et al. (2007) which extends the model by Gibson et al. (1989)
	double computePaxDependentDwellTimeAtStations(int N_BoardPax, int N_AlightPax, double PlatformOccupancyRate, float beta0, float beta1, float beta2, float beta3, float beta4, float beta5, float beta6, float beta7);

	// True when the aspect of the block section is restrictive: red, or red-red at level 5.
	bool isAspectRestrictive(const Section& BS) const;

	// A signal failure makes an End of Authority for every route copy of the failed section, and a held single-track
	// section makes one of the same type for the routes against its holder. Its position is expressed for the
	// direction of its route, so only trains of that direction obey it.
	bool obeysAuthority(const MovementAuthority& authority) const {
		return authority.type != "SignalFailure" || authority.ReversedDirection == train_route[indexOfRoute].reversed_direction;
	}

	// True while a signal failure of the first section of the route holds this train at its entry
	bool isFirstSectionFailed(const Section& firstSection) const;

	double authorityRoutePosition(const MovementAuthority& authority, const Section& section) const {
		return train_route[indexOfRoute].reversed_direction
			? section.GeoXBegNode - authority.AbsPosEoA + section.start_node.X * 1000
			: authority.AbsPosEoA;
	}

	// Resolve only the selected, currently published forward Tale within the EVC's existing scope.
	const MovementAuthority* currentFollowingAuthority(double head, double& leaderHead) const;

	// Validate before current target selection so an invalid old hold cannot clear new metadata.
	void checkIfMovementAuthorityStillValid(double speed, double head, const Section& section);

	// A leader that reports no movement authority any more cannot be followed. Its last one would keep
	// this train in following mode, with the leader's last speed and acceleration as its target.
	void leaveFollowingModeWhenLeaderIsGone(int time_seconds);

	// Function to compute the braking distance in a faster way using the closed formula and average gradient and curvature values (This function has shown the best match to simulated braking curves)
	virtual double BrakingDistanceFastComputation(double V1, double V2, double X1, double X2, Section* BS, int N_BS); // V1 and X1 are the current train speed and position while X2 and V2 are the target position and speed, signalling_block_sections and N_BS are the list of Block Sections and the number of Block Sections along the train route

	// Function to quickly compute the average acceleration distance between two speeds
	virtual double AccelerationTimeFollowingMode(double V1, double V2, double X1, double X2, Section* BS, int N_BS); // V1 and X1 are the current train speed and position while X2 and V2 are the target position and speed, signalling_block_sections and N_BS are the list of Block Sections and the number of Block Sections along the train route

	// Call-local first-match lookup. Earlier future starts invalidate forward progress,
	// so unordered and overlapping routes retain the linear scan's precedence.
	struct BrakingArcCursor {
		const Arc* arc = nullptr;
		int section = 0, arcIndex = 0;
		double lookupHead = 0;
		double earlierFuture = std::numeric_limits<double>::infinity();

		const Arc* find(double head, Section* sections, int count, double* safeUpper = nullptr);
	};

	const Arc* brakingArc(double head, Section* sections, int count, double* safeUpper = nullptr) const;

	// Every curve step samples force at the actual start and uses the right endpoint for distance.
	double brakingCurveSpeed(double speed, const Arc& arc) {
		return std::max(0.0, speed - (max_train_decelaration + total_train_resistances(speed, arc.gradient, arc.curvature) / (total_train_mass * massFactor)) * timestep);
	}

	// Strahl and Sauthoff resistance are quadratic in speed. Invert only the increasing
	// branch of v - dt*a(v), connected to the ordinary small-dt solution. The other
	// quadratic root lies beyond the fold and is not a unique inverse flow.
	// For nextSpeed == 0, q(v) == 0 selects the stopping threshold, not the
	// entire saturated preimage of max(0, q(v)); smaller terminal speeds are valid forward states.
	bool inverseBrakingStep(double nextHead, double nextSpeed, Section* sections, int count, double& head, double& speed);

	// Predict complete full-force continuation after a phase-specific first step.
	// Optional vectors store only a successfully evaluated plan, in driving order.
	bool predictBraking(double head, double speed, double targetSpeed, Section* sections, int count,
		double& end, std::vector<double>* positions = nullptr, std::vector<double>* speeds = nullptr);

	// Fixed-length forward prefix for a positive target whose native plan may dip
	// below the target and recover. Crossing below the target is not completion.
	bool predictBrakingPrefix(double head, double speed, int steps, Section* sections, int count,
		double& end, double& endSpeed, std::vector<double>* positions = nullptr, std::vector<double>* speeds = nullptr);

	// Upper end of the reachable terminal-coordinate interval at a positive ceiling.
	bool brakingTerminalUpper(double head, double speed, double ceiling, Section* sections, int count, double& upper);

	// Select a new terminal control within the actual-state force interval and ceiling.
	// Rounded addition need not be exactly inverted by subtraction and division.
	bool constructBrakingTerminal(double head, double speed, double anchor, double ceiling, Section* sections, int count, double& terminal);

	bool planPositiveBraking(double head, double speed, const Arc& arc, Section* sections, int count, int suggested);

	// Returns an actual-start braking point. Linear interpolation of inverse X/V
	// samples is not a solution of the nonlinear forward flow.
	virtual double BrakDist_Block(double V1, double V2, double X0, Section* BS, int Blocks);

	// Request before the next movement can consume the last opportunity to brake for the entry. The movement
	// bound uses the same force/resistance model and starting-effort bound as acceleration and virtual coupling.
	bool needsSingleTrackReservation(double head, double speed, double entry, Section* sections, int count);

	// A cached successor is a proposal only from its exact actual state and historical target.
	int cachedBrakingSuccessor(int previousInstant, const Arc& arc, double target, double targetSpeed);

	// This Function simulate the capacity of the train driver to react to the first most severe service limit (i.e. it calculates the braking distance and the braking point for satisfying the most severe running operation limit)
	double europeanVitalComputerWithListsImproved(double S, double V, int PreviousInstant, Section* BS, int Blocks, bool& continueCached);

	// Native inverse plan shares the exact actual-start step with BrakDist_Block.
	virtual bool DrawBrakingCurve(double V1, double V2, double X0, Section* BS, int Blocks);

	// Establish a first speed within the actual force range, then predict every
	// subsequent full model step. No linear fractional phase is carried forward.
	bool planBraking(double head, double speed, const Arc& arc, Section* sections, int count);

	void brakingStep(int time_seconds, const Arc& As, Section* BS, int Blocks);

	// One step of braking with the full braking force, for a train that has no braking curve to follow.
	// The deceleration is the one of the braking curve: the full braking force plus the resistances at the speed of the train and the gradient and
	// curvature of the arc As, divided by the mass with its mass factor. On a descent steeper than the braking force holds it is negative and the train gains speed.
	// The new speed is not below zero and the train advances by the mean of the old and the new speed.
	void fullBrakingStep(int time_seconds, const Arc& As);

	// Function for printing Train Trajectory Data
	virtual void PrintTrajectory();

	// Class Destructor
	virtual ~Train() {}

	// This function is used to determine the ID of the TDS the train is currently occupying a block section
	virtual string DetermineOccupiedTDS(int timeinstant, bool IsRouteReversed);

	void recordDirectIncidentId(const std::string& incidentId, double timeSeconds,
		double locationMeters);

	void recordDirectIncident(const SimulationIncident& incident, double timeSeconds,
		double locationMeters);

	void recordDirectSignalFailure(const MovementAuthority& authority, double timeSeconds);

	double effectiveIncidentSpeedLimit(double candidate, int timestepIndex);

	// Function to Simulate the Train Trajectory
	virtual void trajectoryComputationIncludingMovingBlock(int time_seconds, double signalCode1, double signalCode2, double signalCode3);

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

	void recordCurrentServiceStopArrival(int time_seconds);

	bool stationIsOnRoute(int stationIndex, const vector<string>& allowedBlockIds = {}) const;

	double stationRoutePositionMeters(int stationIndex) const;

	void recordStationPassagesAtTime(int time_seconds);

	string stationNameForArrivalStats(int stationIndex) const;

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
	Regional& operator=(const Regional& ob2);
};

// ComputeEnergyConsumptionForAllTrains takes Train* and is called with regional_train.data(),
// so both types must have the same size.
static_assert(sizeof(Regional) == sizeof(Train), "Regional must not add members to Train");

// One element per train of the loaded scene. The storage is created once per build and does not
// grow while the trains are live.
extern std::vector<Regional>& regional_train;

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
