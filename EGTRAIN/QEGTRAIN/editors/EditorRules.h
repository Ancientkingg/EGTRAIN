#pragma once

// SceneDocument.h first: it includes the scene model, which has a member named signals, with the signals keyword of Qt
// undefined, so SceneImporter.h below finds the model included.
#include "editors/SceneDocument.h"
#include "scene/SceneDiagnostic.h"
#include "scene/SceneImporter.h"

#include <QString>

#include <optional>
#include <string>
#include <vector>

/// What an import of legacy passengers adds to a scene. A passenger is rejected when its id is empty or the scene or an
/// earlier accepted passenger of the import has it, or when a journey or leg id of it is empty, repeated inside it, or
/// one that the scene or an earlier accepted passenger has. The ids of a rejected passenger stay free.
struct PassengerImportMerge {
	/// The passengers that are added, in the order of the import.
	std::vector<ScenePassenger> accepted;
	/// The ids of the rejected passengers, in the order of the import. An id can be empty.
	std::vector<std::string> rejected;
	/// The rows that the import report of the scene gets: those of the import, then a "passengers.editor" row when a
	/// passenger was rejected.
	std::vector<SceneImportReportRow> report;

	/// True when the import changes the scene: a passenger was accepted or rejected, or the import reported a row.
	bool changesScene() const { return !accepted.empty() || !report.empty(); }
	/// True when a rejected passenger has this id, which is not empty.
	bool rejects(const std::string& passengerId) const;
};

/// Merges the passengers and the report rows of `imported`, which was read from `sourcePath`, into what `model` has.
PassengerImportMerge mergeImportedPassengers(const SceneModel& model, const ScenePassengerImportResult& imported, const std::string& sourcePath);

/// The signals and then the blocks of the scene, each id once: what a signal failure can have as its target.
std::vector<std::string> signalFailureTargets(const SceneModel& model);

/// One entry of the target combo of an incident.
struct IncidentTargetChoice {
	QString text;				   ///< The text of the entry.
	QString target;				   ///< The signal, block or service it stands for; empty for "Choose target".
	std::optional<int> occurrence; ///< The run of a service, 0 for all runs; absent for an entry that names no service.
	QString toolTip;			   ///< The tooltip of the entry.
};

/// The target combo of an incident: its entries and the one that shows the target of the incident.
struct IncidentTargetChoices {
	std::vector<IncidentTargetChoice> entries; ///< The entries, "Choose target" first.
	int current = 0;						   ///< The entry of the target of the incident.
};

/// The targets that `incident` can have in `model`, with the one it has; only "Choose target" without an incident. A signal
/// failure is offered the signals and the blocks, named by the labels of their sections. A breakdown is offered all runs of
/// its service when it names no run, then each run of each service in turn up to RuntimeLimits::kMaxExpandedTrains runs in
/// all, counted over `occurrenceDuration` seconds, and then the run past that limit that the incident names. A target that
/// the scene does not have is listed as missing. Entries that read the same name their service and run.
IncidentTargetChoices incidentTargetChoices(const SceneModel& model, const SceneIncident* incident, double occurrenceDuration);

/// A scenario read from a file, made ready to be added to a scene.
struct ScenarioImport {
	/// The scenario, with an id and incident ids that the scene does not use yet.
	SceneScenario scenario;
	/// The id that the file gave the scenario.
	std::string requestedId;
	/// The scene already used that id, so the scenario has another.
	bool idTaken = false;
	/// The number of incidents that got another id.
	int renamedIncidents = 0;
	/// The errors that validateScene() finds in the scenario once it is added. The scenario can be added only without
	/// them; errors elsewhere in the scene do not count.
	std::vector<SceneDiagnostic> errors;
};

/// Gives `scenario` an id and incident ids that `model` does not use yet, and checks it as the last scenario of the scene.
ScenarioImport prepareScenarioImport(const SceneModel& model, SceneScenario scenario);
