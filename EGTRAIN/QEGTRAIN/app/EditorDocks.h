#ifndef EDITORDOCKS_H
#define EDITORDOCKS_H

#include <string>
#include <vector>

class CaseSettingsPanel;
class CompositionPanel;
class EntranceDelayPanel;
class IncidentPanel;
class InfrastructurePanel;
class PassengerPanel;
class QDockWidget;
class QMainWindow;
class QShortcut;
class QString;
class RunSelection;
class ScenarioPanel;
class SceneDocument;
class SceneEditorPanel;
class SceneSession;
class ServicePanel;
class TimetableStopsPanel;
class TrainUnitPanel;
class TrainUnitSourceLinker;
class UserPrompts;
struct SceneTrainUnit;
namespace telemetry {
class OperationObservation;
} // namespace telemetry

/// The editor docks at the right of the main window, in one group of tabs: Case Settings, Infrastructure, Rolling Stock
/// Units, Compositions, Services with the stops of the selected service, Scenarios with its Incidents and Entrance delays
/// tabs, and Passengers. It builds the panels and their docks, which the window owns, and connects the panels to each
/// other, to the scene document and, through the Host, to the window.
class EditorDocks {
public:
	/// What the editor docks ask of the window.
	class Host {
	public:
		virtual ~Host() = default;

		/// Puts a dock in the Editors menu and gives it Undo and Redo.
		virtual void registerEditorDock(QDockWidget* dock) = 0;
		/// Takes the pending edits of every editor panel into the scene.
		virtual void commitPendingEditorValues() = 0;
		/// Takes the pending edits and validates the scene again, after an edit in a panel.
		virtual void refreshValidationPanel() = 0;
		/// Takes the pending edits of the Scenario and Passenger panels only, which validation shows again from the scene,
		/// and validates the scene again, after a change that no user action made, such as the reload of a linked file. A
		/// field that is being typed in another panel keeps its text.
		virtual void commitScenarioAndPassengerEditsAndValidate() = 0;
		/// The base time of the case changed: the clock of a run starts this many seconds after midnight.
		virtual void setStartOffset(long long seconds) = 0;
		/// Enables the Infrastructure dock as far as the scene and the run allow, and shows the panel again.
		virtual void refreshInfrastructurePanel(bool resetSelection) = 0;
		/// The selection of the Infrastructure panel changed, which the track preview marks.
		virtual void infrastructureSelectionChanged() = 0;
		/// Shows the scenarios, the incidents and the entrance delays again.
		virtual void refreshIncidentPanel() = 0;
		/// The selected scenario changed, so the results of the last run no longer match the next run.
		virtual void invalidateRunResults() = 0;
		/// Shows the source files of the selected train unit and how they were read.
		virtual void updateTrainUnitSourceStatus() = 0;
		/// Opens the tractive-effort plot of a train unit.
		virtual void plotTrainUnitTraction(const SceneTrainUnit& unit) = 0;
		/// Draws the open traction curves of the train unit `unitId` again, and closes them when it has no curve.
		virtual void refreshInputTractionDiagrams(const std::string& unitId) = 0;
		/// The train unit `oldId` is now `newId`: its open traction curves follow it.
		virtual void renameTractionDiagrams(const QString& oldId, const QString& newId) = 0;
		/// The time in seconds over which the occurrences of a service are counted.
		virtual double serviceOccurrenceDuration() const = 0;
		/// Advanced Details is on, so the Scenario panel shows its details.
		virtual bool advancedDetailsEnabled() const = 0;
		/// The telemetry record of the user action that is going on, which the export of a scenario reports to.
		virtual telemetry::OperationObservation captureTelemetryOperation() const noexcept = 0;
	};

	/// Docks for `window`, which build() fills. The docks do not own `host`.
	EditorDocks(QMainWindow* window, Host& host);

	/// Builds the panels and their docks, adds the docks to the right of the window, hidden, and connects them. The window
	/// calls it once. While it runs, the accessors return what it has built so far, so a Host call in between sees the
	/// panels that exist; before it, they return null. The docks do not own the arguments.
	void build(SceneDocument* document, UserPrompts* prompts, RunSelection& runSelection, SceneSession* session);

	/// The Case Settings panel.
	CaseSettingsPanel* caseSettingsPanel() const { return m_caseSettingsPanel; }
	/// The Case Settings dock.
	QDockWidget* caseSettingsDock() const { return m_caseSettingsDock; }
	/// The Infrastructure panel.
	InfrastructurePanel* infrastructurePanel() const { return m_infrastructurePanel; }
	/// The Infrastructure dock.
	QDockWidget* infrastructureDock() const { return m_infrastructureDock; }
	/// The Rolling Stock Units panel: the physical values and the piecewise traction rows of each unit.
	TrainUnitPanel* trainUnitPanel() const { return m_trainUnitPanel; }
	/// The Rolling Stock Units dock.
	QDockWidget* trainUnitDock() const { return m_trainUnitDock; }
	/// The links of the train units to their source files, which the Rolling Stock Units panel shows.
	TrainUnitSourceLinker* trainUnitSourceLinker() const { return m_trainUnitSourceLinker; }
	/// The Compositions panel.
	CompositionPanel* compositionPanel() const { return m_compositionPanel; }
	/// The Compositions dock.
	QDockWidget* compositionDock() const { return m_compositionDock; }
	/// The Services panel: the fields of a service, with its stops below them.
	ServicePanel* servicePanel() const { return m_servicePanel; }
	/// The ordered stops of the selected service, inside the Services panel.
	TimetableStopsPanel* timetableStopsPanel() const { return m_timetableStopsPanel; }
	/// The Services dock.
	QDockWidget* serviceDock() const { return m_serviceDock; }
	/// The Scenario tab of the Scenarios dock: the scenario library.
	ScenarioPanel* scenarioPanel() const { return m_scenarioPanel; }
	/// The Incidents tab of the Scenarios dock: the incidents of the selected scenario.
	IncidentPanel* incidentPanel() const { return m_incidentPanel; }
	/// Esc while the target of an incident is picked on the network view, which ends the pick. The window enables it only
	/// then.
	QShortcut* endTargetPickShortcut() const { return m_endTargetPickShortcut; }
	/// The Entrance delays tab of the Scenarios dock: the entrance delays of the selected scenario.
	EntranceDelayPanel* entranceDelayPanel() const { return m_entranceDelayPanel; }
	/// The Scenarios dock.
	QDockWidget* incidentDock() const { return m_incidentDock; }
	/// The Passengers panel: the passengers with their journeys and ordered legs.
	PassengerPanel* passengerPanel() const { return m_passengerPanel; }
	/// The Passengers dock.
	QDockWidget* passengerDock() const { return m_passengerDock; }
	/// The editor panels, in the order in which the window takes their pending edits.
	const std::vector<SceneEditorPanel*>& panels() const { return m_panels; }

private:
	QMainWindow* const m_window;
	Host& m_host;
	SceneDocument* m_document = nullptr;
	SceneSession* m_session = nullptr;
	QDockWidget* m_caseSettingsDock = nullptr;
	CaseSettingsPanel* m_caseSettingsPanel = nullptr;
	QDockWidget* m_infrastructureDock = nullptr;
	InfrastructurePanel* m_infrastructurePanel = nullptr;
	QDockWidget* m_trainUnitDock = nullptr;
	TrainUnitPanel* m_trainUnitPanel = nullptr;
	TrainUnitSourceLinker* m_trainUnitSourceLinker = nullptr;
	QDockWidget* m_compositionDock = nullptr;
	CompositionPanel* m_compositionPanel = nullptr;
	QDockWidget* m_serviceDock = nullptr;
	ServicePanel* m_servicePanel = nullptr;
	TimetableStopsPanel* m_timetableStopsPanel = nullptr;
	QDockWidget* m_incidentDock = nullptr;
	ScenarioPanel* m_scenarioPanel = nullptr;
	IncidentPanel* m_incidentPanel = nullptr;
	QShortcut* m_endTargetPickShortcut = nullptr;
	EntranceDelayPanel* m_entranceDelayPanel = nullptr;
	QDockWidget* m_passengerDock = nullptr;
	PassengerPanel* m_passengerPanel = nullptr;
	std::vector<SceneEditorPanel*> m_panels;
};

#endif // EDITORDOCKS_H
