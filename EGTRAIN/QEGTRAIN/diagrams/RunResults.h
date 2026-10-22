#ifndef RUNRESULTS_H
#define RUNRESULTS_H

#include "simulation/InitialParameters.h"

#include <string>
#include <vector>

class Train;

/// One train of the run in the provenance sidecar.
struct RunOccurrenceProvenance {
	std::string serviceId;	   ///< Scene service of the train
	int occurrence = 1;		   ///< Occurrence of the service, from 1
	std::string operatingCode; ///< Operating code of the service
};

/// The scene input of a run and whether it can be loaded again as it was run.
struct RunInputProvenance {
	std::string kind;		   ///< "bundle", "directory" or "unsaved"
	std::string path;		   ///< Absolute path of the saved input, empty when unsaved
	std::string sha256;		   ///< SHA-256 in hex: the recorded one when the file changed since, else the file's; may be empty
	bool dirty = false;		   ///< True when the scene had unsaved edits at the run
	bool reproducible = false; ///< True when the saved input on disk is the input of the run
	std::string status;		   ///< "reproducible" or "non-reproducible"
	std::string reason;		   ///< Why the input is not reproducible, empty when it is
};

/// The inputs and settings of a run, written next to every export as a provenance sidecar.
struct RunProvenance {
	std::string caseName;						   ///< Name of the scene
	int sceneSchemaVersion = 0;					   ///< Schema version of the scene
	RunInputProvenance input;					   ///< The scene input
	std::string appliedScenario;				   ///< Id of the scenario applied to the run, empty for none
	double baseTimeSeconds = 0.0;				   ///< Clock time of second 0, in seconds since midnight
	double durationSeconds = 0.0;				   ///< Length of the run in seconds
	double timestepSeconds = 0.0;				   ///< Length of one step in seconds
	double bufferSeconds = 0.0;					   ///< Buffer time in seconds
	double recoveryPercent = 0.0;				   ///< Running time recovery in percent
	int paxMode = 0;							   ///< 1 when InitialParameters::PAX_GUI was set, else 0
	unsigned long randomSeed = kDefaultRandomSeed; ///< Seed of the passenger sampling
	/// The trains of the run, in run order.
	std::vector<RunOccurrenceProvenance> selectedOccurrences;
};

/// SHA-256 in hex of the bundle file, empty when it cannot be read.
std::string hashSceneBundle(const std::string& bundlePath);
/// SHA-256 in hex of the snapshot of the scene directory, empty when it cannot be read.
std::string hashSceneDirectory(const std::string& sceneDirectory);
/// SHA-256 in hex of a scene input snapshot, empty for an empty snapshot.
std::string hashSceneInputSnapshot(const std::string& snapshot);
/// The provenance of the saved input at `savedPath` ("bundle" or "directory"; an empty path is unsaved). `savedSha256`
/// is the SHA-256 recorded when the input was loaded or saved; the input is reproducible only when the file on disk
/// still has it and the scene is not dirty.
RunInputProvenance captureSavedInput(const std::string& savedPath, const std::string& inputKind,
	bool dirty, const std::string& savedSha256);
/// Writes the artifact and its sidecar `<artifactPath>.provenance.json`. `artifactKind` names the artifact in the
/// sidecar, such as "csv" or "png". False when a write fails; the artifact is then not replaced.
bool writeRunArtifactWithProvenance(const std::string& artifactPath,
	const std::string& artifactKind, const std::string& artifactBytes,
	const RunProvenance& run);
/// As writeRunArtifactWithProvenance, with the provenance of both runs of a delay comparison in the sidecar.
bool writeDelayArtifactWithProvenance(const std::string& artifactPath,
	const std::string& artifactKind, const std::string& artifactBytes,
	const RunProvenance& baselineRun, const RunProvenance& scenarioRun);

/// A result that may not exist. An unavailable value is shown as empty or "-", never as 0.
struct RunResultValue {
	bool available = false; ///< True when value holds a result
	double value = 0.0;		///< The result, in the unit of the field that holds it
};

/// One row of the Run Results table: one train.
struct TrainRunResult {
	std::string trainId;					///< Name of the train in the run
	std::string operatingCode;				///< Operating code of the service
	std::string serviceId;					///< Scene service of the train
	int occurrence = 1;						///< Occurrence of the service, from 1
	double performancePercent = 100.0;		///< Share of the tractive effort and maximum speed the service uses, in percent
	bool hasConfiguredMaximumSpeed = false; ///< True when the service sets a maximum speed
	double configuredMaximumSpeedKmh = 0.0; ///< Maximum speed of the service in km/h; meaningful only when hasConfiguredMaximumSpeed
	double compositionMaximumSpeedMs = 0.0; ///< Maximum speed of the composition in m/s
	/// Maximum speed the train runs at in m/s: compositionMaximumSpeedMs, or the lower of it and the configured speed when
	/// hasConfiguredMaximumSpeed, times performancePercent / 100.
	double appliedMaximumSpeedMs = 0.0;
	double appliedMaximumSpeedKmh = 0.0;		///< appliedMaximumSpeedMs in km/h
	RunResultValue startSeconds;				///< First valid position sample, in seconds of the run
	RunResultValue endSeconds;					///< Last valid position sample, in seconds of the run
	RunResultValue travelSeconds;				///< endSeconds minus startSeconds
	RunResultValue energyConsumedKWh;			///< Energy the train consumed without regenerative braking, in kWh
	RunResultValue energyWithRegenKWh;			///< Energy the train consumed with regenerative braking, in kWh
	RunResultValue substationKWh;				///< Energy requested from the substation without regenerative braking, in kWh
	RunResultValue substationWithRegenKWh;		///< Energy requested from the substation with regenerative braking, in kWh
	std::vector<std::string> directIncidentIds; ///< Incidents that acted directly on the train, in order of first effect
	RunResultValue firstDirectIncidentTime;		///< Time of the first direct incident effect, in seconds of the run
	/// Head position of the train, or the end of its authority, at that effect, in m.
	RunResultValue firstDirectIncidentLocation;
	/// True when a train breakdown of the scenario asks the train to terminate at its destination.
	bool destinationTerminationRequested = false;
	/// True when the request was set and the train reached the end of its route.
	bool destinationTerminated = false;
	/// Entrance delay the scenario applied to this train, in seconds.  Zero with an empty station when none was
	/// applied.  The station uses the same names as TimetableResultRow::stationId; its planned departure already
	/// contains the delay.
	double entranceDelaySeconds = 0.0;
	std::string entranceDelayStationId; ///< Station of the entrance delay, empty for none
};

/// Whether the planned and the simulated time of an arrival or departure exist.  The delay is available only
/// with Available.
enum class TimetableValueStatus {
	Available,			///< Both times exist
	NoPlan,				///< Only the simulated time exists
	NotReached,			///< Only the planned time exists
	NoPlanAndNotReached ///< Neither time exists
};

/// Planned and simulated times of one stop of one train.
struct TimetableResultRow {
	std::string trainId;					  ///< Name of the train in the run
	std::string operatingCode;				  ///< Operating code of the service
	std::string serviceId;					  ///< Scene service of the train
	int occurrence = 1;						  ///< Occurrence of the service, from 1
	std::string stationId;					  ///< Station name used by the arrival statistics
	int journeyIndex = 0;					  ///< Position of the stop in the stops of the train, from 1
	int callIndex = 0;						  ///< Number of the call at this station within the journey, from 1
	RunResultValue plannedArrivalSeconds;	  ///< In seconds of the run
	RunResultValue plannedDepartureSeconds;	  ///< In seconds of the run
	RunResultValue simulatedArrivalSeconds;	  ///< In seconds of the run
	RunResultValue simulatedDepartureSeconds; ///< In seconds of the run; unavailable when the train stays to the end
	RunResultValue arrivalDelaySeconds;		  ///< Simulated minus planned arrival, in seconds
	RunResultValue departureDelaySeconds;	  ///< Simulated minus planned departure, in seconds
	/// Which arrival times exist.
	TimetableValueStatus arrivalStatus = TimetableValueStatus::NoPlanAndNotReached;
	/// Which departure times exist.
	TimetableValueStatus departureStatus = TimetableValueStatus::NoPlanAndNotReached;
};

/// The Run Results table: one row for each train and the totals of the network.
struct RunResults {
	std::vector<TrainRunResult> trains;	   ///< In run order
	RunResultValue networkStartSeconds;	   ///< Earliest startSeconds; unavailable unless every train has its times
	RunResultValue networkEndSeconds;	   ///< Latest endSeconds; unavailable unless every train has its times
	RunResultValue networkTravelSeconds;   ///< networkEndSeconds minus networkStartSeconds
	RunResultValue energyConsumedKWh;	   ///< Sum over the trains in kWh; unavailable unless every train has it
	RunResultValue energyWithRegenKWh;	   ///< Sum over the trains in kWh; unavailable unless every train has it
	RunResultValue substationKWh;		   ///< Sum over the trains in kWh; unavailable unless every train has it
	RunResultValue substationWithRegenKWh; ///< Sum over the trains in kWh; unavailable unless every train has it
};

/// One completed run as values for the delay analysis. It holds no pointer into the simulation, so it outlives the run.
struct DelayRunSnapshot {
	std::string caseRevision;				   ///< Revision of the scene document at the run, as text
	std::string scenarioId;					   ///< Id of the applied scenario, empty for none
	double baseTimeSeconds = 0.0;			   ///< Base time of the scene, in seconds since midnight
	double durationSeconds = 0.0;			   ///< CompletedRunSettings::horizonSteps; seconds with the 1 s timestep
	double timestep = 0.0;					   ///< Length of one step in seconds
	bool hasIncidents = false;				   ///< True when the applied scenario has an incident
	bool hasEntranceDelays = false;			   ///< True when the applied scenario has an entrance delay
	RunProvenance provenance;				   ///< Inputs and settings of the run
	RunResults run;							   ///< Run Results of the run
	std::vector<TimetableResultRow> timetable; ///< Planned and simulated times of the run
};

/// Factor from MJ to kWh that the text outputs and the run results use: 1 / 3.6 rounded to five decimals.
constexpr double kEnergyMJToKWh = 0.27778;
/// An energy in MJ as kWh, with kEnergyMJToKWh.
constexpr double energyMJKWh(double energyMJ) {
	return energyMJ * kEnergyMJToKWh;
}

/// The Run Results of the trains. `timestepSeconds` turns sample indices into seconds; a null train is skipped.
RunResults buildRunResults(const std::vector<const Train*>& trains, double timestepSeconds);
/// One row for each stop of each train, in run and stop order. At most Train::kMaxTimetableStations stops of a
/// train are kept; a null train is skipped.
std::vector<TimetableResultRow> buildTimetableResults(const std::vector<const Train*>& trains);
/// For the Run Results summary: "Incidents acted on 1 of 3 trains". A train counts when the run recorded at least one
/// direct incident effect on it.
std::string directIncidentEvidenceText(const RunResults& results);
/// For the Run Results summary: "196/196 selected" when every ticked occurrence enters the simulation inside the
/// run period, and "196/196 selected, 185 in the run period" when some of them do not.
std::string runOccurrencesText(int selected, int total, int selectedInPeriod);

#endif // RUNRESULTS_H
