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

/// Passenger capacity of a train: 300 for the traction unit and 300 for each wagon.
int trainPassengerCapacity(double numberOfWagons);

/// Occupancy as a ratio of passengers to capacity. An unknown capacity (zero or
/// negative) gives 0, so it never counts as congested.
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

	/// Copies every member of obj2, except that sightReacTime takes the setupTime of obj2.
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
/// True when position (m) is short of the hold-back position before brakingPoint (m), beyond the tolerance.
inline bool isShortOfBrakingPoint(double position, double brakingPoint) {
	return position < brakingPoint - kStopHoldbackM - kStopHoldbackToleranceM;
}
/// True when position (m) is past the hold-back position before stoppingPoint (m), beyond the tolerance.
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
	/// Fills stationBlockSection and stationArc: for each stop, the index of the route section and of the arc whose end node is the
	/// station, or -1. Called after Stations or the route change, so that a step does not search the route for the stations.
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

	/// Sets the energy totals of the train (MJ) from its samples between departure_time and End_Time: the energy consumed plus 10 % for
	/// auxiliaries, the energy requested from the substation (divided by EtaSubst), and both again with the braking power regenerated
	/// at the factor EtaRegBrak.
	void TotalEnergyConsumptionWithAndWithoutRegBraking(double EtaSubst, double EtaRegBrak);

	/// True when letting a train enter at origin in this step could close a crossover ahead of a train that can no longer
	/// brake for it.
	bool entryConflictsWithCrossover(int step, const Section& origin) const;

	/// Lets the waiting train enter its route BS in step i (s): CanEnter becomes true once i >= departure_time and the first section
	/// allows it. Where the first section has a level, the entering train occupies it at once. The rules are under "Route entry" in
	/// docs/architecture/signalling-levels.md.
	void checkEntrance(int i, Section* BS);

	/// Passenger-dependent dwell time (s) by the Fernandez et al. (2007) extension of Gibson et al. (1989), with passengers spread over
	/// the doors at random; 0 when nobody boards or alights.
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

	/// The "TrainEnd" "Tale" authority of this step that matches Last_Received_MA, on a level 4 section from the one at head (m) to
	/// eight sections further, and lies ahead of head; nullptr when there is none. leaderHead receives its route position (m).
	const MovementAuthority* currentFollowingAuthority(double head, double& leaderHead) const;

	/// When IsTrainStoppedForEoA is set, keeps it only while the train stands (speed 0) on section, the section of the authority it
	/// stopped at, with its head (m) less than 4 m before it, and that authority still applies to this train and is still in
	/// run.etcsMa; otherwise clears it and BrakingForEoA. Does nothing when IsTrainStoppedForEoA is false. Called before a new
	/// target is chosen, so that an old hold cannot clear the new one.
	void checkIfMovementAuthorityStillValid(double speed, double head, const Section& section);

	/// A leader that reports no movement authority any more cannot be followed. Its last one would keep
	/// this train in following mode, with the leader's last speed and acceleration as its target.
	void leaveFollowingModeWhenLeaderIsGone(int time_seconds);

	/// Braking distance (m) from speed V1 to V2 (m/s) by the closed formula (V1^2 - V2^2) / (2 a), where a is max_train_decelaration
	/// plus the resistance at the mean speed, on the mean gradient and curvature of the arcs of BS that end in (X1, X2] (m), divided by
	/// total_train_mass * massFactor.
	virtual double BrakingDistanceFastComputation(double V1, double V2, double X1, double X2, Section* BS, int N_BS);

	/// Time (s) to accelerate from V1 to V2 (m/s, V2 above V1) on the mean gradient and curvature of the arcs of BS that end in
	/// (X1, X2] (m), summed over 200 speed steps. When the train cannot accelerate there it is taken on a flat gradient, and when it
	/// cannot accelerate on that either the result is -99999.
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

	/// Speed (m/s) after one full braking step from speed (m/s) on arc. Every curve step samples force at the actual start and uses
	/// the right endpoint for distance.
	double brakingCurveSpeed(double speed, const Arc& arc) {
		return std::max(0.0, speed - (max_train_decelaration + total_train_resistances(speed, arc.gradient, arc.curvature) / (total_train_mass * massFactor)) * timestep);
	}

	/// The state one full braking step before nextHead (m) and nextSpeed (m/s); head and speed receive it. The resistance is
	/// quadratic in speed, and only the root on the increasing branch of v - dt a(v) counts, so false when there is no unique one.
	bool inverseBrakingStep(double nextHead, double nextSpeed, Section* sections, int count, double& head, double& speed);

	/// Runs full braking steps from head (m) and speed (m/s) until the speed is at most targetSpeed; end receives the last head (m).
	/// False when that takes 1999 steps or more or leaves the arcs of the sections. positions and speeds, when given, receive every
	/// state in driving order.
	bool predictBraking(double head, double speed, double targetSpeed, Section* sections, int count,
		double& end, std::vector<double>* positions = nullptr, std::vector<double>* speeds = nullptr);

	/// Runs steps full braking steps from head (m) and speed (m/s), even below a positive target; end and endSpeed receive the last
	/// state. False when the steps leave the arcs of the sections. positions and speeds as for predictBraking.
	bool predictBrakingPrefix(double head, double speed, int steps, Section* sections, int count,
		double& end, double& endSpeed, std::vector<double>* positions = nullptr, std::vector<double>* speeds = nullptr);

	/// The furthest head (m) that one step from head and speed (m/s) reaches with at most full traction and a speed capped at ceiling
	/// (m/s); upper receives it. False when no arc holds head.
	bool brakingTerminalUpper(double head, double speed, double ceiling, Section* sections, int count, double& upper);

	/// The speed (m/s) of one step from head and speed that ends at anchor (m), between full braking and full traction and not above
	/// ceiling; terminal receives it. False when the step cannot end at anchor up to rounding.
	bool constructBrakingTerminal(double head, double speed, double anchor, double ceiling, Section* sections, int count, double& terminal);

	/// Plans braking to a positive target speed Vobmin at Xobmin into Sbrak and Vbrak, trying plan lengths around suggested.
	bool planPositiveBraking(double head, double speed, const Arc& arc, Section* sections, int count, int suggested);

	/// The route position (m) where a train at speed V1 (m/s) has to start full braking to reach V2 (m/s) 0.002 m before X0 (m).
	/// X0 when V1 is not above V2, a position before the route when braking has to start there, and -1 when no position is found.
	virtual double BrakDist_Block(double V1, double V2, double X0, Section* BS, int Blocks);

	/// True when the train at head (m) and speed (m/s) must request the single-track zone starting at entry (m) now, because its next
	/// movement could pass the last point where it can still brake for the entry.
	bool needsSingleTrackReservation(double head, double speed, double entry, Section* sections, int count);

	/// The index in Sbrak and Vbrak of the next state of the stored braking plan, when the train is exactly at a state of it in
	/// step previousInstant with the same target (target m, targetSpeed m/s) and that next step is feasible on arc; -1 otherwise.
	int cachedBrakingSuccessor(int previousInstant, const Arc& arc, double target, double targetSpeed);

	/// The route position (m) where the train at S (m) and V (m/s) has to start braking for the most restrictive target ahead, or a very
	/// large value when there is none. Sets Xobmin and Vobmin to that target; continueCached is true when the stored braking plan goes on.
	double europeanVitalComputerWithListsImproved(double S, double V, int PreviousInstant, Section* BS, int Blocks, bool& continueCached);

	/// Stores in Sbrak, Vbrak and BrakStep the full braking curve from V1 down to V2 (m/s) that ends 0.002 m before X0 (m), built
	/// backwards with inverseBrakingStep. False, with BrakStep -1, when V1 is not above V2 or no curve is found in 2000 steps.
	virtual bool DrawBrakingCurve(double V1, double V2, double X0, Section* BS, int Blocks);

	/// Plans braking from head (m) and speed (m/s) to Vobmin 0.002 m before Xobmin (m): a first speed within the actual force range,
	/// then full braking steps. On success Sbrak, Vbrak and BrakStep hold the plan.
	bool planBraking(double head, double speed, const Arc& arc, Section* sections, int count);

	/// One braking step at time_seconds on arc As: follows the stored plan, makes a new one, or brakes with full force.
	void brakingStep(int time_seconds, const Arc& As, Section* BS, int Blocks);

	/// One step of braking with the full braking force, for a train that has no braking curve to follow.
	/// The deceleration is the one of the braking curve: the full braking force plus the resistances at the speed of the train and the gradient and
	/// curvature of the arc As, divided by the mass with its mass factor. On a descent steeper than the braking force
	/// holds it is negative and the train gains speed.
	/// The new speed is not below zero and the train advances by the mean of the old and the new speed.
	void fullBrakingStep(int time_seconds, const Arc& As);

	/// Writes Traj_Train_<trainDescription>.txt to the TEMP folder of the output: for every valid step the time (s), speed (m/s), head
	/// and tail position (m), power (kW), braking point (m), energy (kWh) and section. A reversed route writes original coordinates.
	virtual void PrintTrajectory();

	/// Does nothing.
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

	/// The lower of candidate (m/s) and the reduced speed of an active breakdown of this train in step timestepIndex. A cap that applies
	/// is recorded as a direct incident.
	double effectiveIncidentSpeedLimit(double candidate, int timestepIndex);

	/// Computes step time_seconds of the train from step time_seconds - 1; Eq[time_seconds] labels the branch taken. signalCode1,
	/// signalCode2 and signalCode3 are not read.
	virtual void trajectoryComputationIncludingMovingBlock(int time_seconds, double signalCode1, double signalCode2, double signalCode3);

	/// Finds the steps in which the head reaches and leaves the route position LocationPosition (m) and writes them to PassingPoint as
	/// Time and Time2 (s), with Position in the coordinates of the original direction. Nothing is written when the train never gets
	/// there, and Time2 is left when it does not leave.
	void computeArrivalAndDepartureAtLocation(double LocationPosition, TrainEvent& PassingPoint);

	/// Fills TimetablePoints, when it is empty, with the arrival and departure at every arc end node of the route that has a station
	/// name (stations and junctions), in route order. The first node of the route is not included.
	void ComputeTimetablingPoints();

	/// Adds the sections that the train covers from tail to head in step i to run.blocksOccupied, with its double switches and
	/// diverging switches, and releases its last section when it has left the route. BS holds the Blocks sections of its route.
	virtual void Det_Section_Occupied_By_Train(int i, Section* BS, int Blocks);

	/// Adds the ends of authority of the train in step i on the level 3 and 4 sections of its route to run.etcsMa: the tail less
	/// ETCS3SafetyMargin (m) as "Tale", the head plus the margin as "Front", and the locks of the switches it covers.
	void ReportPositionToRBC(int i, Section* BS, int N_BS, double ETCS3SafetyMargin);

	/// Appends the blocking time of route section IndexCurrentBS at a fixed-block level; SignallingType "Conventional" or "ETCS2" picks
	/// the approach rule. Times are simulation steps (s) found by searching from step EntryTime + 1; setup, release and sight and
	/// reaction times are seconds. AbsoluteRTSupplement (s) or PercentageRTSupplement (%) is the running time supplement.
	void ComputeBlockingTimeForSingleLocation(int IndexCurrentBS, std::string SignallingType, int EntryTime, double SetupTime, double ReleaseTime, double SightReacTime, double AbsoluteRTSupplement, double PercentageRTSupplement);

	/// Function to Compute the Blocking Times in ETCS level 3 (EntryTime is the time the train actually enters the network in the simulation)
	void ComputeBlockTime_ETCSLevel_3_ForSection(int IndexCurrentBS, int EntryTime, double SetupTime, double ReleaseTime, double SightReacTime, double SafetyMargin, double AbsoluteRTSupplement, double PercentRTSupplement);

	/// Appends a level 4 (virtual coupling) blocking time for each infrastructure element of section IndexCurrentBS, with trains at the
	/// minimum safety distance SafetyMargin (m) except at diverging or converging junctions; the other arguments are those of
	/// ComputeBlockingTimeForSingleLocation.
	void ComputeBlockTime_ETCSLevel4_ForSection_MaxCapacity_Improved(int IndexCurrentBS, int EntryTime, double SetupTime, double ReleaseTime, double SightReacTime, double SafetyMargin, double AbsoluteRTSupplement, double PercentRTSupplement);

	/// Gives the two edges of each diverging switch at level 3 or 4 one blocking time, from the start of occupation of the first edge to
	/// the end of the second, so that the switch counts as one block section.
	void ConnectBlockTimesOfSwitchesForTrainCentricSignalling();

	/// Computes the blocking times of the route section by section with the rule of the section's level: per section at levels 0
	/// ("Conventional") and 2 ("ETCS2"), per infrastructure element at levels 3 and 4. Sections at level 1, level 5 or no level get
	/// none. The search starts at Det_Run_Start_Time; the arguments are those of ComputeBlockingTimeForSingleLocation, SafetyMargin in m.
	void ComputeBlockingTimesInMixedSignallingAreas(double SetupTime, double ReleaseTime, double SightReacTime, double SafetyMargin, double AbsRTSupp, double PercRTSupp);

	/// Sets RunStartTime to the step before the first nonzero speed and returns it in seconds. When the train does not move before
	/// End_Time, RunStartTime keeps its value.
	double Det_Run_Start_Time();

	/// The timetable point of a stop of this train, or nullptr if there is none. A station served more than
	/// once has one timetable point per call, in the order of the stops.
	const TrainEvent* timetablePointOfStop(int stopIndex) const;

	/// Sets the station arrivals from the arrivals at the timetable points.
	/// That is the arrival the timetable results report, so the delay statistics use the same event. A stop
	/// whose timetable point has no arrival (the train did not get there) has no arrival. The positions
	/// recorded during the run only serve a stop that has no timetable point at all.
	void Determine_Actual_Station_Arrivals();

	/// Sets StationDelay, the arrival less the planned arrival, and StationConsecDelay, that delay less the entrance delay and the dwell
	/// disturbances of the earlier stops, for every stop with an arrival and a planned arrival (s); negative values become 0. Also sums
	/// TotalInputDelays.
	void computeArrivalDelaysAtStations();

	/// Like computeArrivalDelaysAtStations, but an early arrival keeps its negative delay, and TotalInputDelays is not changed.
	void Compute_Pos_And_Neg_Arrival_Delays_At_Stations();

	/// Records time_seconds * timestep as the arrival (s) at the first stop named CurrentServiceStop that has none yet.
	void recordCurrentServiceStopArrival(int time_seconds);

	/// True when stop stationIndex lies on the route, on one of allowedBlockIds when that list is not empty.
	bool stationIsOnRoute(int stationIndex, const std::vector<std::string>& allowedBlockIds = {}) const;

	/// The route position (m) of stop stationIndex from its geographic position, or -1 when the stop is not on the route. When no
	/// section holds that position, the geographic position in metres.
	double stationRoutePositionMeters(int stationIndex) const;

	/// Records time_seconds * timestep as the arrival (s) at each stop without an arrival whose route position lies within 5 m of the
	/// head's movement in this step.
	void recordStationPassagesAtTime(int time_seconds);

	/// Name of stop stationIndex for the arrival statistics; "None" for an index outside the stops.
	std::string stationNameForArrivalStats(int stationIndex) const;

	/// In step t, logs the arrival of the train at its last stop and its departure from its first stop. An arrival is also appended to
	/// EGTRAINOutput.txt in the Rescheduling output folder (printTrainArrDepMsg). A first or last stop with X 0 takes its X from
	/// run.infra.stations.
	void checkTrainArrDep(int trainIdx, int t);

	/// Appends a row to FolderName/TrainServicePathDiagram.txt: the train, dispLineID, direction, corridor and its head position (m,
	/// original coordinates) in every step, empty where it does not run. nextServiceRouteID is a route index or -1; a train without
	/// an earlier service (prevIntendedDepTime 0) takes that route first when it runs in the same direction on another corridor.
	void printTrainServicePathDiagram(std::string FolderName, int nextServiceRouteID);

	/// Appends "egtrain,<stationName>,<msgType>,<dispLineID>,<trainIdx>,<t>" to FolderName/EGTRAINOutput.txt and then waits 5 ms.
	void printTrainArrDepMsg(std::string stationName, std::string msgType, int trainIdx, int t, std::string FolderName);

	/// The geographic position (km) in step t of the head, or of the point wagon * train_length / (number_of_wagons + 1) behind it;
	/// -1 when it is on no section of the route. Works on routes that cross regions.
	double trainXPosition(int t, int wagon = 0);

	/// instantaneous speed in km/h at GUI time t, mirroring trainXPosition index mapping; 0 outside the samples
	double speedKmhAt(int t) const {
		if (t < 0 || t >= static_cast<int>(instant_train_speed.size()))
			return 0.0;
		return instant_train_speed[t] * 3.6; // m/s to km/h
	}

	/// Route position of the head in step t in km; 0 outside the samples.
	double positionKmAt(int t) const {
		if (t < 0 || t >= static_cast<int>(instant_spatial_position.size()))
			return 0.0;
		return instant_spatial_position[t] / 1000.0;
	}

	/// Number of trajectory samples, one per step.
	int trajectorySize() const {
		return static_cast<int>(instant_train_speed.size());
	}

	/// Sets earliestActiveTrajectoryIndex to index when it is unset and the train has entered.
	void recordEarliestActiveTrajectoryIndex(int index) {
		earliestActiveTrajectoryIndex = recordEarliestTrajectoryIndex(earliestActiveTrajectoryIndex, index, CanEnter);
	}

	/// Sizes every per-step vector to vec_size samples of 0 and sets End_Time to the last step.
	void setTrainVectorSizesFromInput(int vec_size);
};

/// The train class that the operations builder fills. It adds no member to Train.
class Regional : public Train {

public:
	/// Sets the default rolling stock values, which the operations builder replaces.
	Regional();

	/// Does nothing.
	~Regional() {}

	/// Copies the rolling stock data and the service and incident fields of ob2, and sets the passenger capacity from the wagons. The
	/// traction curve, resistanceCoefficient, route, timetable and trajectory are not copied.
	Regional& operator=(const Regional& ob2);
};

// ComputeEnergyConsumptionForAllTrains takes Train* and is called with SimulationRun::trains.data(),
// so both types must have the same size.
static_assert(sizeof(Regional) == sizeof(Train), "Regional must not add members to Train");

/// Fills run.blocksOccupied and run.blocksConnected for step i with the sections that every train covers
/// (Det_Section_Occupied_By_Train), then updates the single-track locks. See "What happens in a step" in
/// docs/architecture/signalling-levels.md.
void Occupy_Block_Sections_Of_Route(SimulationRun& run, int i);

/// Adds the ends of authority of every train in step i to run.etcsMa (ReportPositionToRBC), with the margin ETCS3SafetyMargin (m).
void ReportAllTrainPositionsToRBC(SimulationRun& run, int i, double ETCS3SafetyMargin);

/// Writes MainFolder/BlockingTimes.txt: for each train its description, then rows of the block or element IDs, the start and end
/// positions (m, GeoPosStart and GeoPosEnd) and the start and end of occupation (s) of the first N_BlockTimeComplete entries of
/// BlockTime.
void PrintTrainBlockingTimes(SimulationRun& run, std::string MainFolder);

/// Writes MainFolder/TimetablePoints.txt: for each train its description and type, then rows of the location names, the positions
/// (twice), the arrival times and the departure times of its timetable points.
void PrintTimetablePoints(SimulationRun& run, std::string MainFolder);

/// Computes the blocking times of every train (Train::ComputeBlockingTimesInMixedSignallingAreas, same arguments) and writes
/// BlockingTimes.txt to OutputFolder.
void ComputeBlockingTimesInMixedSignallingForAllTrains(SimulationRun& run, double SetupTime, double ReleaseTime, double SightReacTime, double SafetyMargin, std::string OutputFolder, double AbsRTSupplement, double PercRTSupplement);

/// Lets every running train protect the station area ahead in step i: when the entrance of a station boundary is one or two sections
/// ahead of its head, StationBoundarySection::protectEntrance runs for it. A platform is booked only for trains whose last stop is
/// "Alm", "Asd" or "Asdz". It does not run when the entrance is two sections ahead and the section between is occupied.
void protectStationAreas(SimulationRun& run, int i);

#endif
