# Scene Editor Flow

## Purpose and users

The scene editor gives students one window flow for opening an assignment scene, editing timetable and incident data, running EGTRAIN, and inspecting the existing diagrams. Instructors need portable bundles, editable scene directories, and clear validation errors. Researchers need readable JSON inputs and reproducible runs without direct legacy-file editing.

## Window flow

- Start state: the main window opens with no scene selected. `Open Case Study...`, `Open Scene Folder...`, recent scenes, `Load Legacy Case...`, and `Quit` are available. Save, scene edit panes, and scene Run are disabled until a scene opens.
- Open scene: `Open Case Study...` selects an `.egscene` bundle; `Open Scene Folder...` selects an editable canonical directory. Both load the same canonical JSON into `SceneModel`. The selected path is added to the recent-scenes list in `QSettings`.
- Validation: opening a scene populates the validation panel with `SceneDiagnostic` entries. Structural errors are shown first. Semantic validation runs only when structural loading has no errors.
- Edit panes: after a valid enough model loads, the editor panes show scene data from `SceneModel`. V1 edits update `SceneModel`; they do not edit legacy files directly.
- Save: `Save Scene` writes back to the opened bundle or directory. `Save Case Study As...` writes a portable `.egscene`; `Save Scene As Folder...` writes canonical JSON to a directory.
- Run handoff: Run revalidates the current model. Error diagnostics block Run. If validation passes, the shared native setup builds the existing runtime globals directly from that model.
- Back to diagrams: after the run, students return to the normal EGTRAIN diagram tools: speed-distance, speed-time, time-distance, delay, train path, and blocking-time diagrams.

`Load Legacy Case...` is an explicit conversion action. It reads a selected
external legacy directory into a new canonical scene; the source files are not
an editable or runtime fallback.

## Main window layout

| Area | Contents | Enabled state |
|---|---|---|
| File menu | `Open Case Study...`, `Open Scene Folder...`, `Save Scene`, `Save Case Study As...`, `Save Scene As Folder...`, recent scenes, `Load Legacy Case...`, `Quit` | Open, recent scenes, legacy case picker, and Quit are always enabled. Save is enabled when a scene is loaded and dirty. Both Save As actions are enabled when a scene is loaded. |
| Simulation menu and toolbar | `Run`, `Pause`, `Stop`, speed control | Run is enabled when a runnable scene is loaded and no scene error diagnostics are current. Pause and Stop are enabled only while simulation is running. |
| Central view | Existing network view and progress bar | Empty at startup. Shows canonical scene infrastructure after open and simulation state after setup and run. |
| Scene editor dock | Overview, infrastructure context, compositions, services and timetable, incidents | Enabled for an opened bundle or directory. A legacy case must first be imported and opened as a scene. |
| Validation panel | Dockable table of diagnostics | Visible after scene open. Updated on open, edit, save, and pre-run validation. |
| Existing info dock | Read-only selected item details for nodes, stations, arcs, connections, signals, trains | Enabled when the network view has selectable items. It stays read-only outside explicit editor controls. |
| Status bar | Current scene name, scene path, dirty state, validation summary, run state | Always visible. Scene-specific fields are empty before a scene opens. |

## Validation lifecycle

Validation runs when a bundle or directory opens, after each committed editor change, after Save or Save As, and immediately before Run. The pre-run validation is mandatory even when the panel already shows no errors.

Structural diagnostics come from the bundle reader or directory loader and the required JSON files. If structural loading has errors, semantic validation is skipped to avoid duplicate noise from a partial model. Semantic diagnostics check references, empty trains or routes, timetable values, dwell times, platform references, incident targets, incident windows, base time, and repeated-service headways.

Save is allowed with semantic errors so students can preserve work in progress. Run is not allowed with any `SceneSeverity::Error`. Warnings and info diagnostics stay visible but do not block Run. Native builder diagnostics are appended to the panel and builder errors block Run.

## Simulation handoff mechanics

Run uses the current `SceneModel`, including unsaved editor changes.
`DispatchController::prepareScene` invokes the infrastructure/signalling builder,
then the operations builder, and prepares the configured output directory. It
does not serialize, export, or stage a second input representation.

Simulation parameters are canonical:

- `numTrackLines`: number of entries in `infrastructure.json.tracks`.
- `N_Routes`: number of routes in `signalling.json`.
- `startingSimulationTime`: `scene.json.base_time`, converted from `HH:MM:SS`.
- `times`: `scene.json.simulation_settings.duration_seconds`.

After these values are set, the existing sequence runs in the current main window:

```text
teardownGUI
simulation.resetState
simulation.prepareScene(SceneModel)
setupGUI
```

Only after setup succeeds does the simulation worker start.

## V1 edit panes

Tables are read-only first. Editing happens through explicit row, cell, add, duplicate, and delete controls inside the editor panes.

| Pane | Editable in V1 | Read-only in V1 |
|---|---|---|
| Overview | None for scene JSON metadata | Scene name, scene path, schema version, base time, units, derived run parameters, validation summary |
| Infrastructure context | None | Stations, platforms, signals, routes, block ids, imported infrastructure context |
| Compositions | Train compositions and unit membership | Full train-unit source files and traction curve structure |
| Services and timetable | Services, composition choice, route choice, stopping patterns, station stops, platform choices, arrival times, departure times, dwell times, entry time, repeated-service headway, per-service performance values | Station definitions, route block definitions, signal definitions |
| Incidents | Signal failure incidents and train breakdown incidents: id, type, target, start time, end time | Target lists derived from signals and services |

Per-service performance values still need new fields in `services.json` and `SceneModel`. Incident edits are part of the native run handoff and require no generated legacy file.

## Explicit out-of-scope list

- Full infrastructure editing in V1.
- Drawing or editing nodes, arcs, tracks, switches, gradients, curves, speed limits, station topology, platform topology, signals, block sections, routes, or corridors.
- Treating legacy files as the editable source of truth.
- Editing legacy source files from the UI.
- Treating an explicit interoperability export as a scene directory.
- Replacing existing diagram windows or diagram export behavior as part of the scene editor flow.
- General-purpose JSON text editing inside the application.

## Consequences for #23 and #24

#23 implements the scene load and save UI:

- File menu actions for opening bundles or folders, saving bundles or folders, recent scenes, and legacy import.
- Main-window scene state: current scene path, current `SceneModel`, dirty flag, recent-scenes `QSettings`, and action enabled state.
- Open dialogs for `.egscene` files and canonical directories.
- Save and Save As behavior for bundles and canonical JSON directories.
- Unsaved-change prompts when opening another scene, loading a legacy case, or quitting.

#24 implements the validation panel:

- Dockable `Scene Validation` panel.
- Table columns for severity, code, message, file, path, and suggested fix.
- Population from `SceneDiagnostic` entries on open, edit, save, and pre-run validation.
- Error summary in the status bar.
- Run gating when any diagnostic has error severity.
- Selection behavior that opens or focuses the related editor row when the diagnostic identifies an item.
