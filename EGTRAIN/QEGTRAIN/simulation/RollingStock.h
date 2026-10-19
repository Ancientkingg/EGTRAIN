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
//   (RollingStockBraking.cpp, RollingStockTrajectory.cpp, test_operationsbuilder.cpp), which read the route of the run and so are
//   defined in SimulationRun.h;
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

#include <cstdio>
#include <cstdlib>
#include <deque>
#include <limits>
#include <vector>
#include <cmath> // std::fabs

extern InitialParameters initial_variables;

struct SimulationRun;

/// Builds the trains, the timetable, the incidents and entrance delays of the selected scenario, the passenger capacity of
/// the platforms and the passengers of a loaded scene into the run. selectedScenarioId and selectedOccurrences are those of
/// DispatchController::prepareScene. The run's random generator is seeded from initial_variables.randomSeed first. When the
/// diagnostics hold an error, nothing else of the run and nothing of initial_variables changes; otherwise initial_variables
/// takes the name, base time, duration, buffer time, running time recovery and the numbers of tracks and routes of the scene.
std::vector<SceneDiagnostic> buildOperationsFromScene(SimulationRun& run, const SceneModel& scene,
	const std::string& selectedScenarioId = {},
	const SceneRunSelection& selectedOccurrences = {});

/// Returns the operations state of the run to that of a new run: the trains and their storage, platforms, passengers,
/// incidents and statistics.
void prepareNativeOperationsState(SimulationRun& run);

/// The reset between scene runs, which is the same as preparing a new run.
void resetNativeOperationsState(SimulationRun& run);

// Passenger capacity of a train: 300 for the traction unit and 300 for each wagon.
int trainPassengerCapacity(double numberOfWagons);

// Occupancy as a ratio of passengers to capacity. An unknown capacity (zero or
// negative) gives 0, so it never counts as congested.
double passengerOccupancyRatio(int passengers, int capacity);


/// The blocking time of one train at one block section, or at one infrastructure element at levels 3 and 4. Times are
/// seconds from the start of the run (steps) and durations seconds; positions are metres. Times and durations are -1
/// until computed.
class BlockingTimes {
public:
	std::string BlockID; ///< ID of the block section, or of the infrastructure element.
	/// ID of the location or block section after this one; "None" after the last element of the route. A VirtualTDSB is
	/// skipped, so it is never a VirtualTDSB.
	std::string NextBlockID;
	std::string trainDescription; ///< trainDescription of the train.
	double StartRunTime;		  ///< When the head enters the section.
	double EndRunTime;			  ///< When the head leaves the section.
	double StartApproachTime;	  ///< When the approach to the section begins.
	double EndApproachTime;		  ///< End of the approach: StartRunTime.
	double StartClearTime;		  ///< Start of the clearing: EndRunTime.
	double EndClearTime;		  ///< When the tail leaves the section.
	double length;				  ///< Length of the section in kilometres (Section::length); 0 for an infrastructure element.
	double StartOccTime;		  ///< Start of the blocking time: StartApproachTime minus setupTime and sightReacTime.
	/// End of the blocking time: EndClearTime plus ReleaseTime, plus RunTimeMargin for a block section.
	double EndOccTime;
	double setupTime;	  ///< Setup time of the route.
	double sightReacTime; ///< Sight and reaction time.
	double ApproachTime;  ///< EndApproachTime minus StartApproachTime.
	double RunTime;		  ///< EndRunTime minus StartRunTime.
	double clearingTime;  ///< EndClearTime minus StartClearTime.
	double ReleaseTime;	  ///< Release time.
	/// Running time supplement in seconds: added to EndOccTime for a block section and to EndRunTime for an element at
	/// levels 3 and 4; -1 when none applies.
	double RunTimeMargin;
	double PosStart;	///< Route position of the start of the section.
	double PosEnd;		///< Route position of the end of the section.
	double GeoPosStart; ///< Scene position of the start of the section.
	double GeoPosEnd;	///< Scene position of the end of the section.
	/// ID of the other edge of the same diverging switch, whose blocking time is joined with this one; "None" otherwise.
	std::string ConnectedBlockingTimeID;
	double PosConnectedBlockingTime; ///< Route position of the other edge of the switch; -1 when there is none.
	/// Speed in m/s of the train that passed the element before this one; -1 when there was none. Level 4 uses it for
	/// the approach time.
	double SpeedPreviousTrain;
	std::string NamePreviousTrain;				 ///< trainDescription of the train that passed the element before; "None" when none.
	std::string NextBlockIDPreviousTrain;		 ///< NextBlockID of that train; "None" when none.
	int SignallingLevel;						 ///< Signalling level of the block section; kSignallingLevelUnset by default.
	bool IsEndOfDivSwitchBeingStartOfADivSwitch; ///< Copy of InfraElement::isEndOfDivSwitchStartOfADivSwitch.
	/// True when the switch was already in the position the train needs, so no extra setup time is needed. True for other
	/// elements.
	bool InfraElementInPositionForTrain;
	bool LocationWithSwitch;				  ///< True when the element is a switch.
	bool IsAlreadyUniformedToConnectedSwitch; ///< True once the blocking time has been joined with that of the other edge of its switch.
	std::string SwitchName;					  ///< InfraElement::SwitchName of a switch; "None" otherwise.
	std::string stationName;				  ///< Name of the station at the element; "None" otherwise.
	bool IsComplete;						  ///< False when a start time is missing or the end comes before the start.

	/// Sets the times, durations and positions to -1, the IDs to "None" and IsComplete to true.
	BlockingTimes();

	/// Copies every member.
	BlockingTimes& operator=(const BlockingTimes& obj2);

	/// Check that the last recorded status of the infrastructure element is in line with what the train needs, and sets
	/// InfraElementInPositionForTrain and the data of the previous train.
	void checkIfLastInfraElementStatusIsInLineWithTrainNeeds(std::string TrainName, std::list<InfraEvent> ListAllInfraEvents);
};

/// Blocking times of one train, indexed like an array. Indexing a non-const list
/// past the end grows it with default-constructed entries; growing never moves
/// existing entries. Indexing a const list never grows it.
class BlockingTimeList {
public:
	/// The entry at index, which is added when the list is shorter. A negative index aborts the program.
	BlockingTimes& operator[](int index) {
		checkIndex(index);
		if (index >= static_cast<int>(entries.size()))
			entries.resize(static_cast<std::size_t>(index) + 1);
		return entries[static_cast<std::size_t>(index)];
	}

	/// The entry at index, or a default entry when the list is shorter. A negative index aborts the program.
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
			EG_LOG(SimSetup, Error) << "BlockTime index " << index << " is negative\n";
			eglog::flush();
			std::abort();
		}
	}

	std::deque<BlockingTimes> entries;
};

// Largest number of trains a scene may expand to. It is a validation and editor
// limit, not a storage size: SimulationRun::trains holds exactly the trains of the scene.
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

/// A train: one occurrence of a service, with its rolling stock, its timetable, and its state and results during the run.
/// The vectors such as instant_train_speed hold one sample per step and are indexed by the step t, which is seconds from
/// the start of the run (see timestep). Positions on the route are metres, speeds m/s, forces N, power W and energy MJ.
/// Arrays indexed by stop hold kMaxTimetableStations entries.
class Train {
public:
	/// The run that the train is part of, which the operations builder sets. The train does not own it.
	SimulationRun* run_ = nullptr;
	double number_of_wagons = 0.0; ///< Number of wagons, without the traction unit.
	double massFactor = 0.0;	   ///< Factor on the mass in the equation of motion (sceneTrainMassFactor).
	/// Mass of the traction unit in kg.
	double mass_of_traction_unit = 0.0, mass_of_a_wagon = 0.0, total_train_mass = 0.0, max_train_decelaration = 0.0, max_train_speed = 0.0, g = 0.0, frontal_wagon_area = 0.0, massPerWagonAxle = 0.0, Jerk = 0.0, train_length = 0.0;
	/// \var Train::mass_of_a_wagon
	/// Mass of one wagon in kg.
	/// \var Train::total_train_mass
	/// Mass of the train in kg: the traction unit plus massPerWagonAxle.
	/// \var Train::max_train_decelaration
	/// Deceleration of full braking in m/s2.
	/// \var Train::max_train_speed
	/// Speed limit of the train in m/s, appliedMaximumSpeedMs.
	/// \var Train::g
	/// Gravity acceleration in m/s2.
	/// \var Train::frontal_wagon_area
	/// Frontal area of a wagon in m2, used in the wagon resistance.
	/// \var Train::massPerWagonAxle
	/// Mass of all wagons together in kg (mass_of_a_wagon times number_of_wagons), not a mass per axle.
	/// \var Train::Jerk
	/// Rate at which the braking deceleration builds up, in m/s3.
	/// \var Train::train_length
	/// Length of the train in metres.

	std::vector<double> instant_train_speed; ///< Speed at each step in m/s.
	int velocityIntervals;					 ///< Number of speed bands of the traction curve, the used entries of Vlb to C2; at most 20.
	/// Lower speed bound of each band of the traction curve; the code compares it with the speed in m/s.
	double Vlb[20] = {}, Vub[20] = {}; ///< Upper speed bound of each band, which the band does not include.
	/// Coefficients of the tractive effort c0 + c1 v + c2 v^2 of each band, which give N for v in m/s.
	double C0[20] = {}, C1[20] = {}, C2[20] = {}; ///< See C0.
	/// \var Train::C1
	/// See C0.

	double resistanceCoefficient = 0.0; ///< Coefficient of the wagon resistance (Sauthoff formula) that multiplies the speed in km/h.
	/// Head position on the route in metres at each step; -9999 at the step after End_Time and 0 after that.
	std::vector<double> instant_spatial_position;
	/// ID of the section of the head at each step; "-" before the train enters and from End_Time + 2 on; empty at End_Time + 1
	/// and on a step held by a breakdown.
	std::vector<std::string> instant_block_section_occupied;
	std::vector<double> instant_train_power_consumption; ///< Power at each step in W; -9999 at the step after End_Time and 0 after that.
	/// Branch-applied wheel effort [N], aligned with trajectory samples; -9999 at the step after End_Time and 0 after that.
	std::vector<double> instant_train_tractive_effort;
	std::vector<double> instant_train_energy_consumption; ///< Energy used up to each step in MJ; negative power counts as zero.
	/// Energy in MJ used without regenerative braking, including EnergyForAuxiliaries.
	double TotalEnergyConsumed, TotalEnergySubstationRequest, TotalEnergyConsWithRegBrak, TotalEnergySubstRequestWithRegBrak;
	/// \var Train::TotalEnergySubstationRequest
	/// TotalEnergyConsumed divided by the substation efficiency, in MJ.
	/// \var Train::TotalEnergyConsWithRegBrak
	/// Energy in MJ with regenerative braking: negative power counts, scaled by the regenerative efficiency, plus
	/// EnergyForAuxiliaries.
	/// \var Train::TotalEnergySubstRequestWithRegBrak
	/// TotalEnergyConsWithRegBrak divided by the substation efficiency, in MJ.

	double EnergyForAuxiliaries = 0.0; ///< Energy for auxiliaries in MJ: 10 % of the traction energy.
	/// Step from which the train may enter, in seconds from the start of the run. The builder sets it to
	/// scheduled_departure_time and then spreads the trains over one hour per direction
	/// (changeTrainDepartureTimesForHourlyTimetabling); runSimulation sets it back to scheduled_departure_time before the first step.
	double departure_time = 0.0;
	int brakingPoint;					   ///< Index in Sbrak and Vbrak used by the last braking step. Nothing reads it.
	double scheduled_departure_time = 0.0; ///< Entry time of the train in the timetable, in seconds from the start of the run.
	/// Last accelerating, cruising or following step, from which brakingEffort counts the braking time (temp + 1 is the
	/// first braking step).
	int temp, stop, counter; ///< 1 while the train follows the stored braking plan (Sbrak, Vbrak), 0 otherwise.
	/// \var Train::stop
	/// Step from which the dwell at a stop counts. Accelerating, cruising and following steps set it, some braking steps
	/// set it while the train moves, and the first standing step at a stop sets it.

	double ID = 0.0;		   ///< Occurrence number of the service, from 1.
	std::string type;		   ///< ID of the service.
	std::string operatingCode; ///< Operating code of this occurrence of the service.
	std::string serviceId;	   ///< ID of the scene service.
	int serviceOccurrence = 1; ///< 1-based occurrence of the service.
	/// Performance of the service in percent; it scales the tractive effort and the maximum speed.
	double servicePerformancePercent = 100.0;
	bool hasConfiguredMaximumSpeed = false; ///< True when the service sets its own maximum speed.
	double configuredMaximumSpeedKmh = 0.0; ///< Maximum speed of the service in km/h.
	double compositionMaximumSpeedMs = 0.0; ///< Maximum speed of the composition in m/s.
	/// Maximum speed in m/s the train runs at: the lower of the two above, scaled by servicePerformancePercent.
	double appliedMaximumSpeedMs = 0.0;
	double appliedMaximumSpeedKmh = 0.0;		  ///< appliedMaximumSpeedMs in km/h.
	std::vector<std::string> directIncidentIds;	  ///< IDs of the incidents that acted on this train directly, in order of first effect.
	double firstDirectIncidentTime = -1.0;		  ///< Time in seconds of the first direct incident; -1 when there is none.
	double firstDirectIncidentLocation = -1.0;	  ///< Position in metres where the first direct incident acted; -1 when there is none.
	bool destinationTerminationRequested = false; ///< True when a breakdown asks to mark the train terminated at the end of its route.
	bool destinationTerminated = false;			  ///< True once such a train has reached the end of its route.
	Arc As;										  ///< Not used: the step and the braking routines keep their own arc.
	Section Bs;									  ///< Copy of the section of the head in the current step.
	double Start_Node_X = 0.0;					  ///< X in kilometres of the start of the route.
	std::string TrainRouteID;					  ///< ID of the route of the train.
	int indexOfRoute;							  ///< Index of the route in RunInfrastructure::routes, from 0.
	std::string trainDescription;				  ///< Service ID and occurrence joined by "-", which names the train everywhere.
	bool direction;								  ///< Copy of Route::reversed_direction of the route of the train. Nothing reads it.
	/// True while the stored braking plan is below its target speed before its last point.
	bool GradientExceptionInBraking;
	std::vector<double> BX; ///< Route position in metres, at each step, where braking for the current target begins.
	/// Stop nodes of the train in service order. Node::dwellTime, StopTime and StepStopped hold the dwell of each stop.
	std::vector<Node> Stations;
	/// Number of stops in use; equals Stations.size() for a train built from a scene
	int numStations;
	/// Capacity of the station-indexed arrays below; stations beyond this cap have no timetable slot
	static constexpr int kMaxTimetableStations = RuntimeLimits::kMaxTimetableStops;
	/// Clamps a served-station count to kMaxTimetableStations and logs a warning when it does.
	static int clampStationCount(int requested, const std::string& trainId);
	/// Index on the route of the section of each stop; -1 when the stop is not on the route.
	int stationBlockSection[kMaxTimetableStations] = {};
	/// Index of the arc of each stop within its section; -1 when not on the route.
	int stationArc[kMaxTimetableStations] = {};
	bool StoppedForServiceStop = false; ///< True while the train stands at a stop for its dwell.
	/// Name of the station of the stop while StoppedForServiceStop is true; empty before the first stop and "None" after
	/// the train has left a stop.
	std::string CurrentServiceStop;
	/// Platform ID of that stop; "None" before the first stop and after the train has left a stop, except at a node with
	/// respectOrder, where it keeps the last platform.
	std::string CurrentServiceStopPlatform;

	/// Arrival at each stop in seconds from the start of the run; -1 when the train did not arrive there.
	double StationArrivals[kMaxTimetableStations];
	std::string StationArrivalNames[kMaxTimetableStations]; ///< Preserves served station names for post-run arrival analysis.
	/// Arrival delay at each stop in seconds: StationArrivals minus ScheduledArrivals; -1 when there is none.
	/// computeArrivalDelaysAtStations makes a negative delay 0.
	double StationDelay[kMaxTimetableStations];
	/// Delay at each stop in seconds without the entrance delay and the dwell disturbances of the earlier stops; -1 when
	/// there is none.
	double StationConsecDelay[kMaxTimetableStations];
	double StationDisturbance[kMaxTimetableStations];	  ///< Disturbance of the dwell at each stop in seconds; 0, since nothing sets it.
	int N_Station_Stopped;								  ///< Set to 0 and never changed.
	double ScheduledArrivals[kMaxTimetableStations] = {}; ///< Planned arrival at each stop in seconds from the start of the run; -1 when the stop has none.
	/// Planned departure at each stop in seconds from the start of the run, moved by an entrance delay at that stop; -1 when
	/// the stop has none. The train does not leave a stop before it.
	double ScheduledDepartures[kMaxTimetableStations] = {};
	int delayed;				   ///< Set to 0 and never changed.
	double TotalInputDelays = 0.0; ///< Sum of the Entrance Delays +stochastic disturbances to dwell times, in seconds.
	double EntranceDelay;		   ///< Entrance delay of the train in seconds from the scenario; 0 when there is none.
	/// Station ID (runtime station name) of the first stop whose planned departure the entrance delay shifted; empty
	/// when none was applied
	std::string entranceDelayStationId;
	bool CanEnter;				 ///< True once checkEntrance let the train enter: the step reached departure_time and the first section allowed it.
	bool OutOfSimulation;		 ///< True once the train has passed the end of its route.
	int counter2;				 ///< Number of steps in which checkEntrance found entry allowed; CanEnter is set at the first.
	bool IsTrainCoupling;		 ///< True when the braking target is a point where the train can couple virtually to the train ahead (level 4).
	bool IsTrainInFollowingMode; ///< This boolean is true when the train is following another one in VC mode
	/// True when a train in following mode falls behind its leader because of speed limits, gradient or curvature, not
	/// because their routes split.
	bool IsInUnintentionalDecoupling;
	bool IsTrainDecoupling;					 ///< This boolean is true when the train is Decoupling from the VC mode
	int CounterFollowingMode;				 ///< Number of steps the train has been in following mode.
	std::string LeadingTrainInFollowingMode; ///< trainDescription of the leader in following mode; "None" otherwise.
	/// Route position in metres of the current braking target.
	double Xobmin, Vobmin;		  ///< Speed in m/s to reach at the current braking target.
	std::vector<double> Xob, Vob; ///< Vobmin at each step.
	/// \var Train::Xob
	/// Xobmin at each step.

	/// Route positions in metres of the stored braking plan, 2,000 entries.
	std::vector<double> Sbrak = std::vector<double>(2000, 0.0), Vbrak = std::vector<double>(2000, 0.0);
	/// \var Train::Vbrak
	/// Speeds in m/s of the stored braking plan.

	/// Index of the last entry of the stored braking plan; -1 when there is none.
	int BrakStep;
	bool BrakingForEoA = false;					  ///< True while the train brakes for an ETCS end of authority on the route.
	MovementAuthority Predicted_MA_To_CoupleAt;	  ///< Authority where the train is predicted to couple virtually to the train ahead.
	MovementAuthority Last_Received_MA;			  ///< Last movement authority applied to the train.
	MovementAuthority Last_MA_StoppedAt;		  ///< Last movement authority the train has stopped at.
	MovementAuthority Predicted_MA_To_DecoupleAt; ///< Authority where the train is predicted to decouple from the train ahead.
	/// Label of the branch of the step that produced each sample: 1 accelerate, 12 cruise, 5 breakdown hold or station dwell,
	/// 6 at a signal, 52, 53 and 55 braking from a stored or a new plan, 54 full braking, 323 hold at an authority, 532 and
	/// 534 checkpoint release and hold, 958 zero speed limit, 1002 following a leader; 0 on other steps before entry and from
	/// End_Time + 2 on.
	std::vector<int> Eq;
	int RunStartTime;			///< Step before the first step with a speed above 0 (Det_Run_Start_Time); 0 until computed.
	BlockingTimeList BlockTime; ///< Blocking times of the train, N_BlockSections of them, which the stage after the steps computes.
	int N_BlockSections;		///< Number of entries of BlockTime.
	int N_BlockTimeComplete;	///< Number of entries of BlockTime that are complete.
	int numOverlaps;			///< Set to 0 and never changed.
	double Final_Delay;			///< Set to 0 and never changed.
	/// Last step of the train: the first step at which its head is at or past the end of its route, or the last step of the
	/// run while it has not reached it.
	int End_Time;
	int earliestActiveTrajectoryIndex = -1;	  ///< First simulation sample in the active run; -1 until the train enters.
	std::list<Location> LocationNames;		  ///< Not filled by the simulation, so empty.
	std::list<std::string> ConflictingTrains; ///< Not filled by the simulation, so empty.
	int N_ConflictingTrains;				  ///< Set to 0 and never changed.
	double ETCS3StoppingPoint;				  ///< Set to -1 and never changed.
	bool IsTrainStoppedForEoA;				  ///< True while the train stands at an end of authority (Last_MA_StoppedAt).
	/// Arrival and departure at each station node on the route, where the train stops or not (ComputeTimetablingPoints).
	std::list<TrainEvent> TimetablePoints;
	int prevIntendedDepTime;  ///< First step that printTrainServicePathDiagram may print; set to 0 and never changed.
	int dispLineID = -1;	  ///< Line ID written to the dispatching output files; nothing sets it, so it is -1.
	int arrivalPlatform = -1; ///< Platform to book at the destination; nothing sets it, so it is -1.
	int reservedPlatform;	  ///< Platform booked at the destination, copied from arrivalPlatform; -1 until booked.
	// std::map<char, std::list<std::pair<int, double>>> trainCorrPosTime; // train position over time for each corridor

	float GibsonDwellTimeParameters[8]; ///< The eight parameters of the passenger dwell model (computePaxDependentDwellTimeAtStations); -1 until set.
	/// Passenger capacity of the train (trainPassengerCapacity).
	int MAX_OnBoard_Passengers, Current_OnBoard_Passengers; ///< Passengers on board now; the train starts empty.

	/// Sets the default values. The vectors stay empty until setTrainVectorSizesFromInput.
	Train();
	/// Pre-compute the (block section, Arc) index for each station Node.
	/// Called after Stations or route changes; eliminates the O(N_BS*total_arcs*numStations)
	/// triple scan from the per-timestep hot path.
	void cacheStationPositions();


	/// Tractive effort in N at speed V in m/s, from the band of the traction curve that holds V, scaled by
	/// servicePerformancePercent. 0 outside every band.
	virtual double tractiveEffort(double V);

	/// Braking force in N after T seconds of braking: it grows with Jerk until the deceleration reaches max_train_decelaration.
	virtual double brakingEffort(double T);


	/// Resistance in N of the traction unit at speed V in m/s
	/// (Modified Strahl Formulae_Italian FS Formulation)
	virtual double traction_unit_resistences(double V);

	/// Resistance in N of the wagons at speed V in m/s, using the Sauthoff Formula
	virtual double wagons_resistances(double V);

	/// Resistance in N of the gradient G, as rise per length (Arc::gradient).
	virtual double gradient_resistances(double G);

	/// Resistance in N of a curve of radius R in metres; 0 for a straight track (R = 0).
	virtual double curvature_resistances(double R);

	/// Sum in N of all resistances at speed V in m/s on gradient G and curve radius R in metres.
	virtual double total_train_resistances(double V, double G, double R);

	/// Stores the eight parameters of the passenger dwell model in GibsonDwellTimeParameters.
	virtual void Initialise_Gibson_Dwell_Time_Parameters(float beta0, float beta1, float beta2, float beta3, float beta4, float beta5, float beta6, float beta7);

	/// Adds the energy of step t to instant_train_energy_consumption and returns the total in MJ.
	double train_energy_consumption(int t);

	/// A completed trajectory can leave a non-finite power sample at End_Time;
	/// treat that boundary sample and its aligned effort as zero, matching
	/// train_energy_consumption().
	void sanitizeTerminalPowerSample();

	/// Function to calculate Total Energy Consumed with and without regenrative braking (the inputs are the Eta of the
	/// Substation and the Eta of the regenerative braking)
	void TotalEnergyConsumptionWithAndWithoutRegBraking(double EtaSubst, double EtaRegBrak);

	/// True when letting a train enter at origin in this step could close a crossover ahead of a train that can no longer
	/// brake for it.
	bool entryConflictsWithCrossover(int step, const Section& origin) const;

	/// Check the entrance possibility: sets CanEnter when the step i has reached departure_time and the first section BS[0]
	/// allows entry.
	void checkEntrance(int i, Section* BS);

	/// Function to compute dwell times based on the interaction with passengers
	/// By default the dwell time is computed based on the microscopic dwell time model by Fernandez et al. (2007) which
	/// extends the model by Gibson et al. (1989)
	double computePaxDependentDwellTimeAtStations(int N_BoardPax, int N_AlightPax, double PlatformOccupancyRate, float beta0, float beta1, float beta2, float beta3, float beta4, float beta5, float beta6, float beta7);

	/// True when the aspect of the block section is restrictive: red, or red-red at level 5.
	bool isAspectRestrictive(const Section& BS) const;

	/// A signal failure makes an End of Authority for every route copy of the failed section, and a held single-track
	/// section makes one of the same type for the routes against its holder. Its position is expressed for the
	/// direction of its route, so only trains of that direction obey it.
	inline bool obeysAuthority(const MovementAuthority& authority) const;

	/// True while a signal failure of the first section of the route holds this train at its entry
	bool isFirstSectionFailed(const Section& firstSection) const;

	/// Position in metres of the authority on the route of this train, for an authority on section.
	inline double authorityRoutePosition(const MovementAuthority& authority, const Section& section) const;

	/// Resolve only the selected, currently published forward Tale within the EVC's existing scope.
	const MovementAuthority* currentFollowingAuthority(double head, double& leaderHead) const;

	/// Validate before current target selection so an invalid old hold cannot clear new metadata.
	void checkIfMovementAuthorityStillValid(double speed, double head, const Section& section);

	/// A leader that reports no movement authority any more cannot be followed. Its last one would keep
	/// this train in following mode, with the leader's last speed and acceleration as its target.
	void leaveFollowingModeWhenLeaderIsGone(int time_seconds);

	/// Function to compute the braking distance in a faster way using the closed formula and average gradient and
	/// curvature values (This function has shown the best match to simulated braking curves)
	/// V1 and X1 are the current train speed and position while X2 and V2 are the target position and speed, BS and
	/// N_BS are the list of Block Sections and the number of Block Sections along the train route
	virtual double BrakingDistanceFastComputation(double V1, double V2, double X1, double X2, Section* BS, int N_BS);

	/// Function to quickly compute the time in seconds to accelerate between two speeds; -99999 when the resistances exceed
	/// the tractive effort. Arguments as in BrakingDistanceFastComputation.
	virtual double AccelerationTimeFollowingMode(double V1, double V2, double X1, double X2, Section* BS, int N_BS);

	/// Finds the arc of the route under a head position in metres, continuing from the previous lookup.
	///
	/// Call-local first-match lookup. Earlier future starts invalidate forward progress,
	/// so unordered and overlapping routes retain the linear scan's precedence.
	struct BrakingArcCursor {
		const Arc* arc = nullptr; ///< Arc of the last lookup; null when there is none.
		/// Index of the section of the last lookup.
		int section = 0, arcIndex = 0; ///< Index of the arc within that section.
		double lookupHead = 0;		   ///< Head position in metres of the last lookup.
		/// Lowest start in metres of an arc beyond the head seen by the scan; infinity when there is none.
		double earlierFuture = std::numeric_limits<double>::infinity();

		/// The arc under head, or null. safeUpper, when given, receives the position up to which the result holds.
		const Arc* find(double head, Section* sections, int count, double* safeUpper = nullptr);
	};

	/// The arc under head in metres on the given sections, or null; a fresh BrakingArcCursor lookup.
	const Arc* brakingArc(double head, Section* sections, int count, double* safeUpper = nullptr) const;

	/// Every curve step samples force at the actual start and uses the right endpoint for distance.
	double brakingCurveSpeed(double speed, const Arc& arc) {
		return std::max(0.0, speed - (max_train_decelaration + total_train_resistances(speed, arc.gradient, arc.curvature) / (total_train_mass * massFactor)) * timestep);
	}

	/// Strahl and Sauthoff resistance are quadratic in speed. Invert only the increasing
	/// branch of v - dt*a(v), connected to the ordinary small-dt solution. The other
	/// quadratic root lies beyond the fold and is not a unique inverse flow.
	/// For nextSpeed == 0, q(v) == 0 selects the stopping threshold, not the
	/// entire saturated preimage of max(0, q(v)); smaller terminal speeds are valid forward states.
	bool inverseBrakingStep(double nextHead, double nextSpeed, Section* sections, int count, double& head, double& speed);

	/// Predict complete full-force continuation after a phase-specific first step.
	/// Optional vectors store only a successfully evaluated plan, in driving order.
	bool predictBraking(double head, double speed, double targetSpeed, Section* sections, int count,
		double& end, std::vector<double>* positions = nullptr, std::vector<double>* speeds = nullptr);

	/// Fixed-length forward prefix for a positive target whose native plan may dip
	/// below the target and recover. Crossing below the target is not completion.
	bool predictBrakingPrefix(double head, double speed, int steps, Section* sections, int count,
		double& end, double& endSpeed, std::vector<double>* positions = nullptr, std::vector<double>* speeds = nullptr);

	/// Upper end of the reachable terminal-coordinate interval at a positive ceiling.
	bool brakingTerminalUpper(double head, double speed, double ceiling, Section* sections, int count, double& upper);

	/// Select a new terminal control within the actual-state force interval and ceiling.
	/// Rounded addition need not be exactly inverted by subtraction and division.
	bool constructBrakingTerminal(double head, double speed, double anchor, double ceiling, Section* sections, int count, double& terminal);

	/// Plans braking to a positive target speed Vobmin at Xobmin into Sbrak and Vbrak, trying plan lengths around suggested.
	bool planPositiveBraking(double head, double speed, const Arc& arc, Section* sections, int count, int suggested);

	/// Returns an actual-start braking point. Linear interpolation of inverse X/V
	/// samples is not a solution of the nonlinear forward flow.
	virtual double BrakDist_Block(double V1, double V2, double X0, Section* BS, int Blocks);

	/// Request before the next movement can consume the last opportunity to brake for the entry. The movement
	/// bound uses the same force/resistance model and starting-effort bound as acceleration and virtual coupling.
	bool needsSingleTrackReservation(double head, double speed, double entry, Section* sections, int count);

	/// A cached successor is a proposal only from its exact actual state and historical target.
	int cachedBrakingSuccessor(int previousInstant, const Arc& arc, double target, double targetSpeed);

	/// This Function simulate the capacity of the train driver to react to the first most severe service limit (i.e. it
	/// calculates the braking distance and the braking point for satisfying the most severe running operation limit)
	double europeanVitalComputerWithListsImproved(double S, double V, int PreviousInstant, Section* BS, int Blocks, bool& continueCached);

	/// Native inverse plan shares the exact actual-start step with BrakDist_Block.
	virtual bool DrawBrakingCurve(double V1, double V2, double X0, Section* BS, int Blocks);

	/// Establish a first speed within the actual force range, then predict every
	/// subsequent full model step. No linear fractional phase is carried forward.
	bool planBraking(double head, double speed, const Arc& arc, Section* sections, int count);

	/// One braking step at time_seconds on arc As: follows the stored plan, makes a new one, or brakes with full force.
	void brakingStep(int time_seconds, const Arc& As, Section* BS, int Blocks);

	/// One step of braking with the full braking force, for a train that has no braking curve to follow.
	/// The deceleration is the one of the braking curve: the full braking force plus the resistances at the speed of the train and the gradient and
	/// curvature of the arc As, divided by the mass with its mass factor. On a descent steeper than the braking force
	/// holds it is negative and the train gains speed.
	/// The new speed is not below zero and the train advances by the mean of the old and the new speed.
	void fullBrakingStep(int time_seconds, const Arc& As);

	/// Function for printing Train Trajectory Data
	virtual void PrintTrajectory();

	// Class Destructor
	virtual ~Train() {}

	/// ID of the track detection section of Bs under the head at step timeinstant; empty when there is none.
	virtual std::string DetermineOccupiedTDS(int timeinstant, bool IsRouteReversed);

	/// Adds incidentId to directIncidentIds, and records time in seconds and location in metres when it is the first.
	void recordDirectIncidentId(const std::string& incidentId, double timeSeconds,
		double locationMeters);

	/// recordDirectIncidentId with the ID of the incident, or its target when it has none.
	void recordDirectIncident(const SimulationIncident& incident, double timeSeconds,
		double locationMeters);

	/// Records the incident behind a signal failure authority.
	void recordDirectSignalFailure(const MovementAuthority& authority, double timeSeconds);

	/// The speed limit candidate in m/s, lowered to the speed cap of an active breakdown of this train at the step.
	double effectiveIncidentSpeedLimit(double candidate, int timestepIndex);

	/// Function to Simulate the Train Trajectory: computes step time_seconds from step time_seconds - 1.
	virtual void trajectoryComputationIncludingMovingBlock(int time_seconds, double signalCode1, double signalCode2, double signalCode3);

	/// Function to compute the arrival and departure of a train from a location with a specific position expressed in [m]
	void computeArrivalAndDepartureAtLocation(double LocationPosition, TrainEvent& PassingPoint);

	/// Function to compute the passing time of a train over each timetable point (i.e. stations, junctions and or relevant signals)
	void ComputeTimetablingPoints();

	/// Function to Determine which Block Section is Occupied by the train
	virtual void Det_Section_Occupied_By_Train(int i, Section* BS, int Blocks);

	/// Function to Report Train Position to RBC (the ETCS3 Safety Margin must be given in m)
	void ReportPositionToRBC(int i, Section* BS, int N_BS, double ETCS3SafetyMargin);

	/// Function to Compute Blocking Times for a single Block Section. This Function allows us to compute blocking time in area with mixed signalling system
	/** IndexCurrentBS is the index of the Block Section in the route of the train. SignallingType is "Conventional" or "ETCS2".*/
	void ComputeBlockingTimeForSingleLocation(int IndexCurrentBS, std::string SignallingType, int EntryTime, double SetupTime, double ReleaseTime, double SightReacTime, double AbsoluteRTSupplement, double PercentageRTSupplement);

	/// Function to Compute the Blocking Times in ETCS level 3 (EntryTime is the time the train actually enters the network in the simulation)
	void ComputeBlockTime_ETCSLevel_3_ForSection(int IndexCurrentBS, int EntryTime, double SetupTime, double ReleaseTime, double SightReacTime, double SafetyMargin, double AbsoluteRTSupplement, double PercentRTSupplement);

	/// Function to Compute the Blocking Times in Virtual Coupling (EntryTime is the time the train actually enters the network in the simulation)
	/** This function uses a principle of max capacity where trains travel at a minimum safety distance from each other over the entire shared route, splitting apart by an absolute braking distance only at those junctions where they diverge or converge*/
	void ComputeBlockTime_ETCSLevel4_ForSection_MaxCapacity_Improved(int IndexCurrentBS, int EntryTime, double SetupTime, double ReleaseTime, double SightReacTime, double SafetyMargin, double AbsoluteRTSupplement, double PercentRTSupplement);

	/// Function to Connect the Blocking Times of edges of diverging switches for ETCS Level 3 and Virtual Coupling. In
	/// this case the diverging switch needs to be considered as a single block section, that is why blocking times of
	/// connected edges need to be accordingly modified
	void ConnectBlockTimesOfSwitchesForTrainCentricSignalling();

	/// Function to Compute the Blocking times in mixed signalling areas
	void ComputeBlockingTimesInMixedSignallingAreas(double SetupTime, double ReleaseTime, double SightReacTime, double SafetyMargin, double AbsRTSupp, double PercRTSupp);

	/// Function to evaluate the time instant in which for the first time the train Starts to accelerate (It's the time
	/// corresponding to the first non-zero speed value)
	double Det_Run_Start_Time();

	/// The timetable point of a stop of this train, or nullptr if there is none. A station served more than
	/// once has one timetable point per call, in the order of the stops.
	const TrainEvent* timetablePointOfStop(int stopIndex) const;

	/// Function to setup the station arrivals of the trains from the arrival at their timetable points.
	/// That is the arrival the timetable results report, so the delay statistics use the same event. A stop
	/// whose timetable point has no arrival (the train did not get there) has no arrival. The positions
	/// recorded during the run only serve a stop that has no timetable point at all.
	void Determine_Actual_Station_Arrivals();

	/// Function compute Arrival delays at stations for a train
	void computeArrivalDelaysAtStations();

	/// Function to calculate train delays taking into account both the positive and negative (arrival ahead the schedule) delays
	void Compute_Pos_And_Neg_Arrival_Delays_At_Stations();

	/// Records time_seconds as the arrival at the current stop when that stop has no arrival yet.
	void recordCurrentServiceStopArrival(int time_seconds);

	/// True when stop stationIndex lies on the route, on one of allowedBlockIds when that list is not empty.
	bool stationIsOnRoute(int stationIndex, const std::vector<std::string>& allowedBlockIds = {}) const;

	/// Route position in metres of stop stationIndex; -1 when it is not on the route.
	double stationRoutePositionMeters(int stationIndex) const;

	/// Records time_seconds as the arrival at each stop without an arrival that the head passed in this step, within 5 m.
	void recordStationPassagesAtTime(int time_seconds);

	/// Name of stop stationIndex for the arrival statistics; "None" for an index outside the stops.
	std::string stationNameForArrivalStats(int stationIndex) const;

	/// check train arrival/departure at/from destination/origin
	void checkTrainArrDep(int trainIdx, int t);

	/// print train service path diagram to file
	void printTrainServicePathDiagram(std::string FolderName, int nextServiceRouteID);

	/// print train arrival at terminal station (append to file)
	void printTrainArrDepMsg(std::string stationName, std::string msgType, int trainIdx, int t, std::string FolderName);

	/// computes train X position in kilometres (works with routes acrossing different regions); -1 when that point is on no section
	/** wagon = 0 by default to return head position*/
	double trainXPosition(int t, int wagon = 0);

	/// instantaneous speed in km/h at GUI time t, mirroring trainXPosition index mapping; 0 outside the samples
	double speedKmhAt(int t) const {
		if (t < 0 || t >= static_cast<int>(instant_train_speed.size()))
			return 0.0;
		return instant_train_speed[t] * 3.6; // m/s to km/h
	}

	/// position in km at GUI time t, mirroring trainXPosition index mapping; 0 outside the samples
	double positionKmAt(int t) const {
		if (t < 0 || t >= static_cast<int>(instant_spatial_position.size()))
			return 0.0;
		return instant_spatial_position[t] / 1000.0;
	}

	/// number of trajectory samples available
	int trajectorySize() const {
		return static_cast<int>(instant_train_speed.size());
	}

	/// Sets earliestActiveTrajectoryIndex to index when it is unset and the train has entered.
	void recordEarliestActiveTrajectoryIndex(int index) {
		earliestActiveTrajectoryIndex = recordEarliestTrajectoryIndex(earliestActiveTrajectoryIndex, index, CanEnter);
	}

	/// set vector sizes with length of simulation from user input: vec_size samples, and End_Time to the last
	void setTrainVectorSizesFromInput(int vec_size);
};

/// The train class that the operations builder fills. It adds no member to Train.
class Regional : public Train {

public:
	/// Sets the default rolling stock values, which the operations builder replaces.
	Regional();

	// Regional Destructor
	~Regional() {
		/*PrintTrajectory();*/
	}

	// moved PrintTrajectory to simulation code because here destructor automatically deletes vectors before print function

	/// Copies the rolling stock data and the service and incident fields of another train. The traction curve, the
	/// resistance coefficient, the timetable and the trajectory are not copied.
	Regional& operator=(const Regional& ob2);
};

// ComputeEnergyConsumptionForAllTrains takes Train* and is called with SimulationRun::trains.data(),
// so both types must have the same size.
static_assert(sizeof(Regional) == sizeof(Train), "Regional must not add members to Train");

// Function to Determine for each Route the Block Sections that are occupied by trains (This Function Fill in the list BlocksOccupied)
void Occupy_Block_Sections_Of_Route(SimulationRun& run, int i);

// Function to report the position of the trains in ETCS Level 3
void ReportAllTrainPositionsToRBC(SimulationRun& run, int i, double ETCS3SafetyMargin);

// Function to Print out the blocking times of all the Trains
void PrintTrainBlockingTimes(SimulationRun& run, std::string MainFolder);

// Function to Print out the blocking times of all the Trains
void PrintTimetablePoints(SimulationRun& run, std::string MainFolder);

// Function to compute Blocking Times of trains in mixed signalling areas
void ComputeBlockingTimesInMixedSignallingForAllTrains(SimulationRun& run, double SetupTime, double ReleaseTime, double SightReacTime, double SafetyMargin, std::string OutputFolder, double AbsRTSupplement, double PercRTSupplement);

// function to protect all station areas
void protectStationAreas(SimulationRun& run, int i);

#endif
