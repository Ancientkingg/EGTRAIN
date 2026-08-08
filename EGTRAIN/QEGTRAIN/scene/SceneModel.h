#ifndef SCENEMODEL_H
#define SCENEMODEL_H

#include "scene/SceneDiagnostic.h"
#include <array>
#include <set>
#include <string>
#include <vector>

struct SceneSimulationSettings {
	bool hasDuration = false;
	double durationSeconds = 0.0;
	bool hasBufferTime = false;
	double bufferTimeSeconds = 0.0;
	bool hasRecoveryTime = false;
	double recoveryTimePercent = 0.0;
};

struct SceneTrack { std::string id; };

struct SceneNode {
	std::string id;
	std::string trackId;
	double xKm = 0.0;
	double yKm = 0.0;
};

struct SceneArc {
	std::string id;
	std::string trackId;
	std::string fromNodeId;
	std::string toNodeId;
	double curvatureRadiusM = 0.0;
	double gradientPercent = 0.0;
	double speedLimitMs = 0.0;
};

struct SceneBlock {
	std::string id;
	std::string trackId;
	double lengthKm = 0.0;
};

struct SceneConnection {
	std::string id;
	std::string fromNodeId;
	std::string toNodeId;
	bool hasSpeedLimit = false;
	double speedLimitMs = 0.0;
};

struct ScenePlatform {
	std::string id;
	std::vector<std::string> nodeIds;
};

struct SceneStation {
	std::string id;
	std::string name;
	bool hasPosition = false;
	double positionKm = 0.0;
	std::vector<ScenePlatform> platforms;
};

struct SceneSignal { std::string id; };

struct SceneRoute {
	std::string id;
	std::vector<std::string> blocks;
	bool hasCorridor = false;
	std::string corridor;
	bool reversed = false;
};

struct SceneBlockDependency {
	std::string block;
	std::string dependsOn;
};

struct SceneSingleTrackRestriction {
	std::string startBlock;
	std::string endBlock;
	std::string protectedStartBlock;
	std::string protectedEndBlock;
};

struct SceneStationBoundary {
	std::string entranceBlock;
	bool hasExitBlock = false;
	std::string exitBlock;
	bool direction = false;
};

struct SceneTrainPhysical {
	double mass_of_traction_unit_kg = 0.0;
	double mass_of_a_wagon_kg = 0.0;
	double number_of_wagons = 0.0;
	double max_speed_ms = 0.0;
	double max_deceleration_ms2 = 0.0;
	double frontal_area_m2 = 0.0;
	double resistance_coefficient = 0.0;
	double jerk_ms3 = 0.0;
	double length_m = 0.0;
};

struct SceneTrainUnit {
	std::string id;
	bool hasPhysical = false;
	SceneTrainPhysical physical;
	std::vector<std::array<double, 5>> tractionCurve;
	std::string sourceDataFile;
	std::string sourceTractionFile;
};

struct SceneComposition {
	std::string id;
	std::vector<std::string> units;
};

struct SceneStop {
	std::string stationId;
	std::string platformId;
	bool hasPlannedArrival = false;
	bool hasPlannedDeparture = false;
	double plannedArrivalSeconds = 0.0;
	double plannedDepartureSeconds = 0.0;
	double dwellSeconds = 0.0;
};

struct SceneService {
	std::string id;
	std::string operatingCode;
	std::string composition;
	std::string route;
	bool through = false;
	bool hasEntryTime = false;
	double entryTimeSeconds = 0.0;
	bool hasRepeat = false;
	double headwaySeconds = 0.0;
	std::vector<SceneStop> stops;
};

struct SceneIncident {
	std::string id;
	std::string type; // "signal_failure" | "train_breakdown"
	std::string target;
	double startSeconds = 0.0;
	double endSeconds = 0.0;
};

struct SceneEntranceDelay {
	std::string serviceId;
	int occurrence = 1;
	std::string stationId;
	double delaySeconds = 0.0;
};

struct SceneScenario {
	std::string id;
	std::string name;
	std::string description;
	std::vector<SceneIncident> incidents;
	std::vector<SceneEntranceDelay> entranceDelays;
};

struct ScenePassengerLeg {
	std::string id;
	std::string originStationId;
	std::string destinationStationId;
	std::string serviceId;
	int occurrence = 1;
};

struct ScenePassengerJourney {
	std::string id;
	std::string activity;
	std::string originStationId;
	std::string destinationStationId;
	double plannedDepartureStartSeconds = 0.0;
	double plannedDepartureEndSeconds = 0.0;
	double plannedArrivalStartSeconds = 0.0;
	double plannedArrivalEndSeconds = 0.0;
	std::vector<ScenePassengerLeg> legs;
};

struct ScenePassenger {
	std::string id;
	std::vector<ScenePassengerJourney> journeys;
};

struct SceneLoadedData {
	std::string category;
	std::string sourceFile;
	int parsedCount = 0;
	std::string status;
	std::vector<SceneLoadedData> children;
};

struct SceneImportReportRow {
	std::string category;
	std::string sourceFile;
	int sourceCount = 0;
	int convertedCount = 0;
	int skippedCount = 0;
	int unresolvedReferences = 0;
};

struct SceneModel {
	int schemaVersion = 0;
	std::string name;
	std::string description;
	std::string baseTime;
	SceneSimulationSettings settings;

	std::vector<SceneLoadedData> loadedData;
	std::set<std::string> sourceFiles;
	std::vector<SceneImportReportRow> importReport;
	std::vector<SceneTrack> tracks;
	std::vector<SceneNode> nodes;
	std::vector<SceneArc> arcs;
	std::vector<SceneBlock> blocks;
	std::vector<SceneConnection> connections;
	std::vector<SceneStation> stations;
	std::vector<SceneSignal> signals;
	std::vector<SceneRoute> routes;
	std::vector<SceneBlockDependency> blockDependencies;
	std::vector<SceneSingleTrackRestriction> singleTrackRestrictions;
	std::vector<SceneStationBoundary> stationBoundaries;
	std::vector<SceneTrainUnit> trainUnits;
	std::vector<SceneComposition> compositions;
	std::vector<SceneService> services;
	std::string defaultScenarioId;
	std::vector<SceneScenario> scenarios;
	std::vector<ScenePassenger> passengers;
};

// GUI/editor callers use the preferred scenario without a duplicate flat
// incident vector in SceneModel.
SceneScenario* defaultScenario(SceneModel& scene);
const SceneScenario* defaultScenario(const SceneModel& scene);
std::vector<SceneIncident>& defaultScenarioIncidents(SceneModel& scene);
const std::vector<SceneIncident>& defaultScenarioIncidents(const SceneModel& scene);

struct SceneLoadResult {
	SceneModel scene; // partial on structural failure
	std::vector<SceneDiagnostic> diagnostics;
};

SceneLoadResult loadScene(const std::string& sceneDir);
void refreshLoadedDataSummary(SceneModel& scene);
void refreshLoadedDataDiagnostics(SceneModel& scene, const std::vector<SceneDiagnostic>& diagnostics);
void refreshSavedSceneMetadata(SceneModel& scene);

#endif // SCENEMODEL_H
