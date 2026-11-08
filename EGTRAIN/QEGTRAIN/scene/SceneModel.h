#ifndef SCENEMODEL_H
#define SCENEMODEL_H

#include "scene/SceneDiagnostic.h"
#include <array>
#include <cstddef>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

/// The schema_version of scene.json that this application reads and writes.
inline constexpr int kCurrentSceneSchemaVersion = 1;
/// The bundle_version of an .egscene bundle that this application reads and writes.
inline constexpr int kCurrentSceneBundleVersion = 1;

/// The simulation_settings of scene.json. Each value counts only when its has* flag is set.
struct SceneSimulationSettings {
	bool hasDuration = false;		  ///< True when duration_seconds is given.
	double durationSeconds = 0.0;	  ///< Length of a run in seconds, counted from the base time.
	bool hasBufferTime = false;		  ///< True when buffer_time_seconds is given.
	double bufferTimeSeconds = 0.0;	  ///< Buffer time in seconds, rounded to whole seconds for the blocking times.
	bool hasRecoveryTime = false;	  ///< True when recovery_time_percent is given.
	double recoveryTimePercent = 0.0; ///< Recovery time in percent, rounded to a whole percent for the blocking times.
};

/// A track: a chain of nodes joined by arcs, with its own kilometre chainage.
struct SceneTrack { std::string id; /**< Unique id of the track. */ };

/// How the preview draws one track (views.json). It does not change the simulation.
struct SceneTrackView {
	std::string trackId; ///< The track it applies to.
	int level = 0;		 ///< Lateral offset of the track in the preview, in steps of a fixed distance.
	int region = 0;		 ///< Region id whose station anchors place the track in the preview.
	bool visible = true; ///< False leaves the track out of the preview; the simulation still uses it.
	/// True draws the track as a double switch: without its first and last arc and without node dots. The
	/// simulation still uses every arc.
	bool doubleSwitch = false;
};

/// A point of a track.
struct SceneNode {
	std::string id;		 ///< Unique id of the node.
	std::string trackId; ///< The track the node lies on.
	double xKm = 0.0;	 ///< Chainage along the track in kilometres.
	double yKm = 0.0;	 ///< Second coordinate in kilometres.
};

/// The stretch of a track between two nodes, with its geometry and speed limit.
struct SceneArc {
	std::string id;				   ///< Unique id of the arc.
	std::string trackId;		   ///< The track the arc lies on.
	std::string fromNodeId;		   ///< Node at the start, in the direction of the track.
	std::string toNodeId;		   ///< Node at the end, in the direction of the track.
	double curvatureRadiusM = 0.0; ///< Radius of curvature in metres; 0 means straight.
	/// Rise per length, so 0.01 is a 1 % slope, despite the name and the key gradient_percent. The value is
	/// stored and simulated unchanged; a route driven against the track direction sees the opposite sign.
	double gradientPercent = 0.0;
	double speedLimitMs = 0.0; ///< Speed limit in m/s.
};

/// A block of a track. The blocks of a track follow each other in file order from the first node of the
/// track, each as long as lengthKm.
struct SceneBlock {
	std::string id;		   ///< Unique id of the block. It cannot contain '/'.
	std::string trackId;   ///< The track the block lies on.
	double lengthKm = 0.0; ///< Length along the chainage of the track in kilometres.
};

/// A connection between two nodes. Each pair of blocks that hold its nodes gives a switch section.
struct SceneConnection {
	std::string id;				///< Unique id of the connection.
	std::string fromNodeId;		///< One node.
	std::string toNodeId;		///< The other node.
	bool hasSpeedLimit = false; ///< True when speed_limit_ms is given.
	double speedLimitMs = 0.0;	///< Speed limit over the switch in m/s.
};

/// Length of a platform in metres when the platform states none.
inline constexpr double kDefaultPlatformLengthM = 100.0;
/// Width of a platform in metres when the platform states none.
inline constexpr double kDefaultPlatformWidthM = 2.5;

/// A platform of a station.
struct ScenePlatform {
	std::string id;							  ///< Id of the platform, unique within its station.
	std::vector<std::string> nodeIds;		  ///< The nodes at which trains stop at this platform.
	bool hasLength = false;					  ///< True when length_m is given.
	double lengthM = kDefaultPlatformLengthM; ///< Length in metres.
	bool hasWidth = false;					  ///< True when width_m is given.
	double widthM = kDefaultPlatformWidthM;	  ///< Width in metres.
};

/// Passenger capacity of a platform of this length and width in metres, before it is rounded to the
/// integer the runtime stores: the area divided by pi times 0.8 squared, times 0.8.
double scenePlatformCapacity(double lengthM, double widthM);

/// A station with its platforms.
struct SceneStation {
	std::string id;			  ///< Unique id of the station.
	std::string name;		  ///< Display name; the id is shown when it is empty.
	bool hasPosition = false; ///< True when position_km is given.
	/// Chainage in kilometres. A station without platforms binds to the nodes at this chainage.
	double positionKm = 0.0;
	std::vector<ScenePlatform> platforms; ///< The platforms, in file order.
};

/// Where the preview places a station (views.json). It does not change the simulation.
struct SceneStationView {
	std::string stationId;	///< The station it applies to.
	double latitude = 0.0;	///< Latitude in degrees.
	double longitude = 0.0; ///< Longitude in degrees.
	/// Pairs of a region id and the chainage in kilometres at which the station lies on the tracks of
	/// that region.
	std::vector<std::pair<int, double>> regions;
	std::vector<std::string> corridors; ///< Corridor names of the station.
};

/// A signal.
struct SceneSignal {
	std::string id;				  ///< Unique id of the signal.
	std::string protectedSection; ///< Section reference of the section the signal protects; empty when none.
};

/// A stretch of chainage with one signalling level.
struct SceneSignallingArea {
	std::string id;		  ///< Unique id of the area.
	double startKm = 0.0; ///< Start of the stretch in kilometres of chainage.
	double endKm = 0.0;	  ///< End of the stretch in kilometres of chainage.
	/// Signalling level 0 to 5 (SignallingLevel), or kSignallingLevelUnset for an area without a chosen system.
	int level = 0;
	std::string trackId; ///< The track the area applies to. Empty means network-wide.
};

/// A route: the sections a train runs over, in travel order.
struct SceneRoute {
	std::string id;					 ///< Unique id of the route.
	std::vector<std::string> blocks; ///< Section references in travel order.
	bool hasCorridor = false;		 ///< True when corridor is given.
	std::string corridor;			 ///< Corridor name that groups routes in the result views.
	/// True when the route runs against the direction of its tracks. It has to agree with the order of blocks.
	bool reversed = false;
};

/// Adds section dependsOn to the sections that the builder treats as joined to section block by a switch, beside
/// the switch sections derived from block.
struct SceneBlockDependency {
	std::string block;	   ///< Section reference of the section that gets the dependency.
	std::string dependsOn; ///< Section reference of the section added to it.
};

/// A single-track stretch that trains of one direction hold at a time.
struct SceneSingleTrackRestriction {
	std::string startBlock;			 ///< First plain section of the stretch, in either order with endBlock.
	std::string endBlock;			 ///< Last plain section of the stretch.
	std::string protectedStartBlock; ///< Section at one end where a passing loop joins the stretch.
	/// Section at the other end where a passing loop joins the stretch; endBlock again for a stub track.
	std::string protectedEndBlock;
};

/// The entrance of a station area. For a train that approaches it on a route of the given direction, the
/// entrance is held occupied while a section after it, up to the exit, is occupied.
struct SceneStationBoundary {
	std::string entranceBlock; ///< Section reference of the entrance section.
	bool hasExitBlock = false; ///< True when exit_block is given.
	std::string exitBlock;	   ///< Section reference of the last section checked; without it, to the route end.
	/// The routes it applies to: true for routes driven in reverse, false for routes driven forward.
	bool direction = false;
};

/// The physical data of a train unit, in the units their names state.
struct SceneTrainPhysical {
	double mass_of_traction_unit_kg = 0.0; ///< Mass of the traction unit in kg.
	double mass_of_a_wagon_kg = 0.0;	   ///< Mass of one wagon in kg.
	double number_of_wagons = 0.0;		   ///< Number of wagons.
	double max_speed_ms = 0.0;			   ///< Maximum speed in m/s.
	double max_deceleration_ms2 = 0.0;	   ///< Maximum braking deceleration in m/s².
	double frontal_area_m2 = 0.0;		   ///< Frontal area in m², used in the wagon resistance.
	/// Factor of the speed term of the wagon resistance. The simulation multiplies it by the speed in km/h.
	double resistance_coefficient = 0.0;
	double jerk_ms3 = 0.0; ///< Rate of change of the braking deceleration in m/s³.
	double length_m = 0.0; ///< Length of the unit in metres.
};

/// A train unit: its physical data and its tractive effort curve.
struct SceneTrainUnit {
	std::string id;				 ///< Unique id of the unit.
	bool hasPhysical = false;	 ///< True when the physical object is given.
	SceneTrainPhysical physical; ///< The physical data; meaningful only when hasPhysical is set.
	/// Rows of [v_lower, v_upper, c0, c1, c2]: the tractive effort in N is c0 + c1 v + c2 v² for a speed v
	/// from v_lower up to v_upper. The simulation compares the bounds with speeds in m/s.
	std::vector<std::array<double, 5>> tractionCurve;
	std::string sourceDataFile;		///< Path of the vehicle data file the editor reads the physical data from; empty when none.
	std::string sourceTractionFile; ///< Path of the traction file the editor reads the curve from; empty when none.
};

/// A composition: the train units that run together as one train.
struct SceneComposition {
	std::string id;					///< Unique id of the composition.
	std::vector<std::string> units; ///< Train unit ids in order; one id may repeat.
};

/// Runtime-ready composition data shared by the native operations builder and the legacy compatibility
/// exporter.
struct SceneCompositionRuntime {
	SceneTrainPhysical physical;					  ///< The combined physical data, as buildSceneComposition makes it.
	std::vector<std::array<double, 5>> tractionCurve; ///< The combined curve, in the form of SceneTrainUnit::tractionCurve.
	/// The units of the composition in order. They point into the source scene, which owns them.
	std::vector<const SceneTrainUnit*> units;
};

/// A planned call of a service at a station. Times are seconds after the base time of the scene.
struct SceneStop {
	std::string stationId;				  ///< The station of the call.
	std::string platformId;				  ///< The platform of that station; empty when not given.
	bool hasPlannedArrival = false;		  ///< True when planned_arrival_seconds is given.
	bool hasPlannedDeparture = false;	  ///< True when planned_departure_seconds is given.
	double plannedArrivalSeconds = 0.0;	  ///< Planned arrival in seconds after the base time.
	double plannedDepartureSeconds = 0.0; ///< Planned departure in seconds after the base time.
	double dwellSeconds = 0.0;			  ///< Least time the train stands at the stop, in seconds.
};

/// A service: a train of one composition on one route, with its timetable, possibly repeated.
struct SceneService {
	std::string id;			   ///< Unique id of the service; it does not change for repeated occurrences.
	std::string operatingCode; ///< Operating code shown for the train; empty means the id. Services may share one.
	std::string composition;   ///< Id of the composition.
	std::string route;		   ///< Id of the route.
	/// Percentage, 1 to 100, of the tractive effort and of the commanded maximum speed. Braking is not scaled.
	double performancePercent = 100.0;
	bool hasMaximumSpeed = false; ///< True when maximum_speed_kmh is given.
	/// Speed cap of the service in km/h. The lower of it and the composition maximum applies.
	double maximumSpeedKmh = 0.0;
	bool through = false;			   ///< The legacy through flag. The stops decide the calls.
	bool hasEntryTime = false;		   ///< True when entry_time_seconds is given.
	double entryTimeSeconds = 0.0;	   ///< Entry into the network in seconds after the base time.
	bool hasRepeat = false;			   ///< True when the service repeats.
	double headwaySeconds = 0.0;	   ///< Time between occurrences in seconds.
	bool hasRepeatCount = false;	   ///< True when repeatCount is given.
	int repeatCount = 0;			   ///< Total number of occurrences, the first one included.
	bool hasOperatingCodeStep = false; ///< True when operatingCodeStep is given.
	/// Amount added to a numeric operating code for each later occurrence. A step of 0 is invalid.
	int operatingCodeStep = 0;
	std::vector<SceneStop> stops; ///< The planned calls in order. Empty means no calls.
	std::string category;		  ///< Category label for display; empty means none. It does not change the simulation.
	/// Display colour as "#RRGGBB"; empty means the default colour. It does not change the simulation.
	std::string visualizationColor;
};

/// One run of a service: the service id with a 1-based occurrence number.
struct SceneServiceOccurrence {
	std::string serviceId; ///< The service.
	int occurrence = 1;	   ///< The occurrence, counted from 1.

	/// True when both members are equal.
	bool operator==(const SceneServiceOccurrence& other) const {
		return serviceId == other.serviceId && occurrence == other.occurrence;
	}
	/// True when a member differs.
	bool operator!=(const SceneServiceOccurrence& other) const {
		return !(*this == other);
	}
	/// Orders by service id, then by occurrence.
	bool operator<(const SceneServiceOccurrence& other) const {
		return serviceId < other.serviceId
			|| (serviceId == other.serviceId && occurrence < other.occurrence);
	}
};

/// The occurrences chosen for a run. An empty set means every occurrence.
using SceneRunSelection = std::set<SceneServiceOccurrence>;

/// An incident of a scenario. Times are seconds after the base time of the scene.
struct SceneIncident {
	std::string id;	  ///< Unique id of the incident.
	std::string type; ///< "signal_failure" or "train_breakdown".
	/// A signal id or section reference for a signal failure, a service id for a breakdown.
	std::string target;
	double startSeconds = 0.0; ///< Start in seconds after the base time.
	/// End in seconds after the base time; the incident lasts from startSeconds up to it.
	double endSeconds = 0.0;
	// The optional breakdown fields follow the first five members, so an aggregate initializer of those
	// five gives a breakdown that holds the train until endSeconds.
	/// True when occurrence is given. An occurrence other than 1 also counts; otherwise every occurrence is targeted.
	bool hasOccurrence = false;
	int occurrence = 1;			  ///< The occurrence of the target service, counted from 1.
	bool hasReducedSpeed = false; ///< True when reduced_speed_kmh is given. A nonzero reducedSpeedKmh also counts.
	/// Speed cap in km/h during a breakdown, instead of a hold. Without an end it lasts to the route end.
	double reducedSpeedKmh = 0.0;
	bool terminateAtDestination = false; ///< True when the train is to end its run at its route end.
	bool hasEndSeconds = false;			 ///< True when end_seconds is given. A nonzero endSeconds also counts as an end.
};

/// A delay added to the planned departure of one occurrence at one of its stops.
struct SceneEntranceDelay {
	std::string serviceId;	   ///< The service.
	int occurrence = 1;		   ///< The occurrence, counted from 1.
	std::string stationId;	   ///< The station; the first stop of the occurrence there gets the delay.
	double delaySeconds = 0.0; ///< Delay in seconds.
};

/// A named set of incidents and entrance delays.
struct SceneScenario {
	std::string id;									///< Unique id of the scenario.
	std::string name;								///< Display name.
	std::string description;						///< Free text; empty when none.
	std::vector<SceneIncident> incidents;			///< The incidents.
	std::vector<SceneEntranceDelay> entranceDelays; ///< The entrance delays.
};

/// One leg of a passenger journey: a ride on one occurrence of a service.
struct ScenePassengerLeg {
	std::string id;					  ///< Id of the leg, unique across all journeys.
	std::string originStationId;	  ///< Station where the passenger boards.
	std::string destinationStationId; ///< Station where the passenger alights.
	std::string serviceId;			  ///< The service ridden.
	int occurrence = 1;				  ///< The occurrence of that service, counted from 1.
};

/// A journey of a passenger. Its times are seconds since midnight, not since the base time.
struct ScenePassengerJourney {
	std::string id;							   ///< Id of the journey, unique across all passengers.
	std::string activity;					   ///< Activity label; empty when none.
	std::string originStationId;			   ///< First station of the journey.
	std::string destinationStationId;		   ///< Last station of the journey.
	double plannedDepartureStartSeconds = 0.0; ///< Start of the planned departure window, in seconds since midnight.
	double plannedDepartureEndSeconds = 0.0;   ///< End of the planned departure window, in seconds since midnight.
	double plannedArrivalStartSeconds = 0.0;   ///< Start of the planned arrival window, in seconds since midnight.
	double plannedArrivalEndSeconds = 0.0;	   ///< End of the planned arrival window, in seconds since midnight.
	std::vector<ScenePassengerLeg> legs;	   ///< The legs in order; may be empty.
};

/// A passenger with its journeys.
struct ScenePassenger {
	std::string id;								 ///< Unique id of the passenger.
	std::vector<ScenePassengerJourney> journeys; ///< The journeys.
};

/// The stops of a service where a passenger leg boards and alights.
struct SceneServiceStopPair {
	std::size_t originIndex = 0;	  ///< Index into SceneService::stops of the boarding stop.
	std::size_t destinationIndex = 0; ///< Index into SceneService::stops of the alighting stop.
};

/// A row of import_report in scene.json. The counts describe the conversion source, not a run.
struct SceneImportReportRow {
	std::string category;		  ///< The kind of record converted.
	std::string sourceFile;		  ///< The legacy file it was read from; empty when not given.
	int sourceCount = 0;		  ///< Records in the source.
	int convertedCount = 0;		  ///< Records converted.
	int skippedCount = 0;		  ///< Records skipped.
	int unresolvedReferences = 0; ///< References that could not be resolved.
};

/// The whole scene: the content of the scene files and the summaries derived from them.
struct SceneModel {
	int schemaVersion = 0;			 ///< schema_version of scene.json; 0 when not read.
	std::string savedWithAppVersion; ///< Version of the application that saved the scene; empty when not recorded.
	std::string name;				 ///< Name of the scene.
	std::string description;		 ///< Free text; empty when none.
	/// Clock time "HH:MM:SS" at which a run starts. Times of services and incidents count from it.
	std::string baseTime;
	SceneSimulationSettings settings; ///< The simulation settings.

	std::set<std::string> sourceFiles;				///< Names of the scene files present at load or save; derived.
	std::vector<SceneImportReportRow> importReport; ///< Rows of import_report.
	std::vector<SceneTrack> tracks;					///< infrastructure.json tracks.
	std::vector<SceneTrackView> trackViews;			///< views.json tracks.
	std::vector<SceneNode> nodes;					///< infrastructure.json nodes.
	std::vector<SceneArc> arcs;						///< infrastructure.json arcs.
	std::vector<SceneBlock> blocks;					///< infrastructure.json blocks.
	std::vector<SceneConnection> connections;		///< infrastructure.json connections.
	std::vector<SceneStation> stations;				///< stations.json stations.
	std::vector<SceneStationView> stationViews;		///< views.json stations.
	/// signalling.json signals. Code that includes Qt reads them through sceneSignals().
	std::vector<SceneSignal> signals;
	std::vector<SceneSignallingArea> signallingAreas;				  ///< signalling.json signalling_areas.
	std::vector<SceneRoute> routes;									  ///< signalling.json routes.
	std::vector<SceneBlockDependency> blockDependencies;			  ///< signalling.json block_dependencies.
	std::vector<SceneSingleTrackRestriction> singleTrackRestrictions; ///< signalling.json single_track_restrictions.
	std::vector<SceneStationBoundary> stationBoundaries;			  ///< signalling.json station_boundaries.
	std::vector<SceneTrainUnit> trainUnits;							  ///< rolling_stock.json train_units.
	std::vector<SceneComposition> compositions;						  ///< rolling_stock.json compositions.
	std::vector<SceneService> services;								  ///< services.json services.
	std::string defaultScenarioId;									  ///< Id of the scenario a run uses.
	std::vector<SceneScenario> scenarios;							  ///< scenarios.json scenarios.
	std::vector<ScenePassenger> passengers;							  ///< passengers.json passengers.
};

/// A new scene: current schema, base time 08:00:00, a duration of 3600 s, zero buffer and recovery
/// time, and the baseline scenario.
SceneModel makeNewSceneModel();

/// The scenario of a case that stores none: id "baseline", no incidents and no entrance delays.
SceneScenario makeBaselineScenario();

/// The scene name as one component of an output path, or "scene" when the name is not a valid file name
/// on Windows.
std::string sceneOutputDirectoryComponent(const std::string& sceneName);
/// Accepts exactly "#" followed by six hexadecimal digits in either case, and then sets each given
/// channel to 0 to 255.
bool sceneParseVisualizationColor(const std::string& text, int* red = nullptr, int* green = nullptr,
	int* blue = nullptr);
/// The visualization_color text of the service with this id; empty when the
/// service has none or the id is unknown.
std::string sceneServiceVisualizationColor(const SceneModel& model, const std::string& serviceId);
/// Entry time of an occurrence in seconds after the base time: entry_time_seconds, else the planned
/// departure of the first stop, else 0, plus (occurrence - 1) headways when the service repeats.
double sceneServiceScheduledEntry(const SceneService& service, int occurrence = 1);
/// Number of occurrences whose entry lies in [0, durationSeconds). A non-empty selection counts only
/// the selected occurrences of this service. 0 when the duration is not positive.
int sceneServiceInWindowCount(const SceneService& service, double durationSeconds,
	const SceneRunSelection& selection = {});
/// Number of occurrences in a run of this many seconds: 1 without repeat, the repeat count when given,
/// otherwise the duration divided by the headway, rounded up, and at least 1.
int sceneServiceOccurrenceCount(const SceneService& service, double durationSeconds);
/// Operating code of an occurrence: the code plus the step per occurrence when a step is set,
/// "code-occurrence" for another repeated service, the code itself otherwise. Empty when it cannot be made.
std::string sceneServiceOccurrenceOperatingCode(const SceneService& service, int occurrence);
/// Finds the first stop at the leg origin and the first later stop at its destination. Returns false,
/// leaving result unchanged, when there is no such pair.
bool resolveScenePassengerLegStops(const SceneService& service, const ScenePassengerLeg& leg,
	SceneServiceStopPair& result);

/// Resolves the ordered, possibly repeated, unit references of a composition. A single unit is copied. Two or more
/// are combined: traction unit masses, wagon counts and lengths add up, the wagon mass becomes the mean per wagon,
/// the resistance coefficient the mass-weighted mean, the lowest speed, deceleration and jerk apply, the first
/// frontal area is kept, and the traction curves add up per speed band. Returns false and sets diagnostic on failure.
bool buildSceneComposition(const SceneModel& scene, const std::string& compositionId,
	SceneCompositionRuntime& result, std::string& diagnostic);

/// Gravitational acceleration [m/s^2] that the gradient and braking limits are
/// derived from.
inline constexpr double kSceneGravityMs2 = 9.81;

/// Factor on the total train mass in the equation of motion: 1.09 on the traction unit mass and 1.06 on
/// the wagon mass, divided by the total mass.
double sceneTrainMassFactor(const SceneTrainPhysical& physical);

/// The scenario that defaultScenarioId names, or the first one when it names none. Adds the baseline
/// scenario when the scene has none, and sets defaultScenarioId when it is empty.
SceneScenario* defaultScenario(SceneModel& scene);
/// The scenario that defaultScenarioId names, or the first one when it names none; null when there is none.
const SceneScenario* defaultScenario(const SceneModel& scene);
/// A scenario as a list shows it: its name, or its ID when it has none, and in brackets the markers, such as "default".
/// When another scenario of the scene shows the same name, its ID comes first in the brackets, so that the two can be
/// told apart: "New scenario (new_scenario_2, modified)".
std::string sceneScenarioLabel(const SceneModel& scene, const SceneScenario& scenario,
	const std::vector<std::string>& markers = {});
/// The incidents of defaultScenario(scene).
std::vector<SceneIncident>& defaultScenarioIncidents(SceneModel& scene);
/// The incidents of defaultScenario(scene); an empty list when the scene has no scenario.
const std::vector<SceneIncident>& defaultScenarioIncidents(const SceneModel& scene);

/// The signals of the scene. Code that includes Qt reads them here, because Qt defines signals as a keyword
/// and so SceneModel::signals does not compile there.
std::vector<SceneSignal>& sceneSignals(SceneModel& scene);
/// The signals of the scene, read-only.
const std::vector<SceneSignal>& sceneSignals(const SceneModel& scene);

/// When saveScene writes a file of a scene directory.
enum class SceneFileWrite {
	Always,			///< On every save.
	WithViews,		///< When the scene has track or station views.
	WithPassengers, ///< When the scene has passengers.
	Never,			///< Never; the loader still reads it from an older scene.
};

/// A file of a scene directory.
struct SceneFile {
	const char* name;	  ///< The file name.
	bool required;		  ///< True when loadScene cannot load a directory without it.
	SceneFileWrite write; ///< When saveScene writes it.
};

/// Every file of a scene directory, in the order in which saveScene writes them. The snapshot of a directory holds all of
/// them. A bundle holds the files that saveScene writes, and those that it always writes are required there.
inline constexpr std::array<SceneFile, 10> kSceneFiles{{
	{"scene.json", true, SceneFileWrite::Always},
	{"infrastructure.json", true, SceneFileWrite::Always},
	{"stations.json", true, SceneFileWrite::Always},
	{"signalling.json", true, SceneFileWrite::Always},
	{"rolling_stock.json", true, SceneFileWrite::Always},
	{"services.json", true, SceneFileWrite::Always},
	{"scenarios.json", false, SceneFileWrite::Always},
	{"views.json", false, SceneFileWrite::WithViews},
	{"passengers.json", false, SceneFileWrite::WithPassengers},
	{"incidents.json", false, SceneFileWrite::Never},
}};

/// True when name is the name of a file in kSceneFiles.
bool isSceneFileName(const std::string& name);
/// True when saveScene writes file for scene.
bool sceneFileWritten(const SceneFile& file, const SceneModel& scene);

/// The result of loading a scene directory or bundle.
struct SceneLoadResult {
	SceneModel scene;						  ///< The scene; partial on structural failure.
	std::vector<SceneDiagnostic> diagnostics; ///< Problems found while loading.
	/// The bytes read: for a directory its scene files as buildSceneDirectorySnapshot encodes them, for a
	/// bundle the bundle file.
	std::string inputSnapshot;
	std::optional<int> bundleVersion; ///< bundle_version of a bundle; empty for a directory.
};

/// The scene files of a directory as one byte string.
struct SceneInputSnapshot {
	std::string bytes;	///< The files as buildSceneDirectorySnapshot encodes them; empty on failure.
	std::string reason; ///< Why the files could not be read; empty on success.
};

/// Encodes pairs of a file name and its content, sorted by name, as "name length:name:content length:content"
/// one after the other.
std::string buildSceneDirectorySnapshot(
	const std::vector<std::pair<std::string, std::string>>& files);
/// Reads the six required scene files and the optional scenarios, incidents, passengers and views files of
/// a directory into a snapshot.
SceneInputSnapshot readSceneDirectorySnapshot(const std::string& sceneDir);

/// Reads a scene directory. The diagnostics cover files, JSON shape and field types; validateScene checks
/// the rest.
SceneLoadResult loadScene(const std::string& sceneDir);
/// Sets sourceFiles to the files that a save writes.
void refreshSavedSceneMetadata(SceneModel& scene);

#endif // SCENEMODEL_H
