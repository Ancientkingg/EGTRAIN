#ifndef RUNRESULTS_H
#define RUNRESULTS_H

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
	std::vector<RunOccurrenceProvenance> selectedOccurrences;
};

std::string hashSceneBundle(const std::string& bundlePath);
std::string hashSceneDirectory(const std::string& sceneDirectory);
RunInputProvenance captureSavedInput(const std::string& savedPath, const std::string& inputKind,
		bool dirty);
bool writeRunProvenanceSidecar(const std::string& artifactPath, const std::string& artifactKind,
		const RunProvenance& run);
bool writeDelayProvenanceSidecar(const std::string& artifactPath, const std::string& artifactKind,
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

struct DelayComparisonRow {
	std::string serviceId;
	int occurrence = 1;
	std::string operatingCode;
	RunResultValue baselineFinalArrival;
	RunResultValue scenarioFinalArrival;
	RunResultValue positiveContribution;
	std::string attribution; // "primary" or "secondary"
	std::vector<std::string> incidentIds;
	RunResultValue firstDirectTime;
	RunResultValue firstDirectLocation;
	bool destinationTerminationRequested = false;
	bool destinationTerminated = false;
};

struct DelayComparisonResult {
	bool valid = false;
	std::string diagnostic;
	std::vector<DelayComparisonRow> rows;
	RunResultValue totalArrivalDelay;
};

// Preserve the legacy MJ-to-kWh conversion used by text outputs.
constexpr double kEnergyMJToKWh = 0.27778;
constexpr double energyMJKWh(double energyMJ) {
	return energyMJ * kEnergyMJToKWh;
}

RunResults buildRunResults(const std::vector<const Train*>& trains, double timestep);
std::vector<TimetableResultRow> buildTimetableResults(const std::vector<const Train*>& trains);
DelayComparisonResult compareDelayRuns(const DelayRunSnapshot& baseline,
		const DelayRunSnapshot& scenario);

#endif // RUNRESULTS_H
