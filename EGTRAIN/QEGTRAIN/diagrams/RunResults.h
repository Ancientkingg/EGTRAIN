#ifndef RUNRESULTS_H
#define RUNRESULTS_H

#include "simulation/InitialParameters.h"

#include <string>
#include <vector>

class Train;

struct RunOccurrenceProvenance {
	std::string serviceId;
	int occurrence = 1;
	std::string operatingCode;
};

struct RunInputProvenance {
	std::string kind;
	std::string path;
	std::string sha256;
	bool dirty = false;
	bool reproducible = false;
	std::string status;
	std::string reason;
};

struct RunProvenance {
	std::string caseName;
	int sceneSchemaVersion = 0;
	RunInputProvenance input;
	std::string appliedScenario;
	double baseTimeSeconds = 0.0;
	double durationSeconds = 0.0;
	double timestepSeconds = 0.0;
	double bufferSeconds = 0.0;
	double recoveryPercent = 0.0;
	int paxMode = 0;
	int tsmMode = 0;
	int routeChoiceMode = 0;
	unsigned long randomSeed = kDefaultRandomSeed;
	std::vector<RunOccurrenceProvenance> selectedOccurrences;
};

std::string hashSceneBundle(const std::string& bundlePath);
std::string hashSceneDirectory(const std::string& sceneDirectory);
std::string hashSceneInputSnapshot(const std::string& snapshot);
RunInputProvenance captureSavedInput(const std::string& savedPath, const std::string& inputKind,
	bool dirty, const std::string& savedSha256);
bool writeRunArtifactWithProvenance(const std::string& artifactPath,
	const std::string& artifactKind, const std::string& artifactBytes,
	const RunProvenance& run);
bool writeDelayArtifactWithProvenance(const std::string& artifactPath,
	const std::string& artifactKind, const std::string& artifactBytes,
	const RunProvenance& baselineRun, const RunProvenance& scenarioRun);

struct RunResultValue {
	bool available = false;
	double value = 0.0;
};

struct TrainRunResult {
	std::string trainId;
	std::string operatingCode;
	std::string serviceId;
	int occurrence = 1;
	double performancePercent = 100.0;
	bool hasConfiguredMaximumSpeed = false;
	double configuredMaximumSpeedKmh = 0.0;
	double compositionMaximumSpeedMs = 0.0;
	double appliedMaximumSpeedMs = 0.0;
	double appliedMaximumSpeedKmh = 0.0;
	RunResultValue startSeconds;
	RunResultValue endSeconds;
	RunResultValue travelSeconds;
	RunResultValue energyConsumedKWh;
	RunResultValue energyWithRegenKWh;
	RunResultValue substationKWh;
	RunResultValue substationWithRegenKWh;
	std::vector<std::string> directIncidentIds;
	RunResultValue firstDirectIncidentTime;
	RunResultValue firstDirectIncidentLocation;
	bool destinationTerminationRequested = false;
	bool destinationTerminated = false;
	// Entrance delay the scenario applied to this train, in seconds.  Zero with an empty station when none was
	// applied.  The station uses the same names as TimetableResultRow::stationId; its planned departure already
	// contains the delay.
	double entranceDelaySeconds = 0.0;
	std::string entranceDelayStationId;
};

// Whether the planned and the simulated time of an arrival or departure exist.  Available: both.  NoPlan: only the
// simulated time.  NotReached: only the planned time.  NoPlanAndNotReached: neither.  The delay is available only
// with Available.
enum class TimetableValueStatus {
	Available,
	NoPlan,
	NotReached,
	NoPlanAndNotReached
};

struct TimetableResultRow {
	std::string trainId;
	std::string operatingCode;
	std::string serviceId;
	int occurrence = 1;
	std::string stationId;
	int journeyIndex = 0;
	int callIndex = 0;
	RunResultValue plannedArrivalSeconds;
	RunResultValue plannedDepartureSeconds;
	RunResultValue simulatedArrivalSeconds;
	RunResultValue simulatedDepartureSeconds;
	RunResultValue arrivalDelaySeconds;
	RunResultValue departureDelaySeconds;
	TimetableValueStatus arrivalStatus = TimetableValueStatus::NoPlanAndNotReached;
	TimetableValueStatus departureStatus = TimetableValueStatus::NoPlanAndNotReached;
};

struct RunResults {
	std::vector<TrainRunResult> trains;
	RunResultValue networkStartSeconds;
	RunResultValue networkEndSeconds;
	RunResultValue networkTravelSeconds;
	RunResultValue energyConsumedKWh;
	RunResultValue energyWithRegenKWh;
	RunResultValue substationKWh;
	RunResultValue substationWithRegenKWh;
};

// A frozen pair of completed runs.  It owns values only; no runtime Train
// pointers survive completion.
struct DelayRunSnapshot {
	std::string caseRevision;
	std::string scenarioId;
	double baseTimeSeconds = 0.0;
	double durationSeconds = 0.0;
	double timestep = 0.0;
	bool hasIncidents = false;
	bool hasEntranceDelays = false;
	RunProvenance provenance;
	RunResults run;
	std::vector<TimetableResultRow> timetable;
};

// Preserve the legacy MJ-to-kWh conversion used by text outputs.
constexpr double kEnergyMJToKWh = 0.27778;
constexpr double energyMJKWh(double energyMJ) {
	return energyMJ * kEnergyMJToKWh;
}

RunResults buildRunResults(const std::vector<const Train*>& trains, double timestepSeconds);
std::vector<TimetableResultRow> buildTimetableResults(const std::vector<const Train*>& trains);
// For the Run Results summary: "Direct incident evidence recorded for 1 of 3 trains". A train counts when the
// run recorded at least one direct incident effect on it.
std::string directIncidentEvidenceText(const RunResults& results);

#endif // RUNRESULTS_H
