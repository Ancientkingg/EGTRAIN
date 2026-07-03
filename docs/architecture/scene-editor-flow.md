# Scene Editor Flow

## Purpose and users

The scene editor gives students one window flow for opening an assignment scene, editing timetable and incident data, running EGTRAIN, and inspecting the existing diagrams. Instructors need repeatable scene directories and clear validation errors. Researchers need readable JSON inputs and reproducible runs without direct legacy-file editing.

## Window flow

- Start state: the main window opens with no scene selected. `Open Scene...`, recent scenes, `Load Legacy Case...`, and `Quit` are available. Save, scene edit panes, and scene Run are disabled until a scene opens.
- Open scene: `Open Scene...` selects a scene directory. The canonical JSON files load into `SceneModel`; that directory is the single source of truth. The path is added to the recent-scenes list in `QSettings`.
- Validation: opening a scene populates the validation panel with `SceneDiagnostic` entries. Structural errors are shown first. Semantic validation runs only when structural loading has no errors.
- Edit panes: after a valid enough model loads, the editor panes show scene data from `SceneModel`. V1 edits update `SceneModel`; they do not edit legacy files directly.
- Save: `Save Scene` writes `SceneModel` back to the opened scene directory JSON files. `Save Scene As...` writes the same model to a selected scene directory. The `legacy/` passthrough folder is kept untouched or copied as passthrough data.
- Run handoff: Run revalidates the current model. Error diagnostics block Run. If validation passes with no errors, the model is exported to a temporary legacy input folder and the existing simulator setup sequence runs from that folder.
- Back to diagrams: after the run, students return to the normal EGTRAIN diagram tools: speed-distance, speed-time, time-distance, delay, train path, and blocking-time diagrams.

`Load Legacy Case...` keeps the existing hardcoded case picker. It loads a legacy case through the old path and does not make legacy files the editable scene source.

## Main window layout

| Area | Contents | Enabled state |
|---|---|---|
| File menu | `Open Scene...`, `Save Scene`, `Save Scene As...`, recent scenes, `Load Legacy Case...`, `Quit` | Open, recent scenes, legacy case picker, and Quit are always enabled. Save is enabled when a scene is loaded and dirty. Save As is enabled when a scene is loaded. |
| Simulation menu and toolbar | `Run`, `Pause`, `Stop`, speed control | Run is enabled when a scene or legacy case is loaded and no scene error diagnostics are current. Pause and Stop are enabled only while simulation is running. |
| Central view | Existing network view and progress bar | Empty or legacy state at startup. Shows scene infrastructure context after open. Shows simulation state after setup and run. |
| Scene editor dock | Overview, infrastructure context, compositions, services and timetable, incidents | Enabled only for opened scene directories. Disabled for hardcoded legacy cases unless they are imported and opened as scenes. |
| Validation panel | Dockable table of diagnostics | Visible after scene open. Updated on open, edit, save, and pre-run validation. |
| Existing info dock | Read-only selected item details for nodes, stations, arcs, connections, signals, trains | Enabled when the network view has selectable items. It stays read-only outside explicit editor controls. |
| Status bar | Current scene name, scene path, dirty state, validation summary, run state | Always visible. Scene-specific fields are empty before a scene opens. |

## Validation lifecycle

Validation runs on `Open Scene...`, after each committed editor change, after Save or Save As, and immediately before Run. The pre-run validation is mandatory even when the panel already shows no errors.

Structural diagnostics come from loading the scene directory and required JSON files. If structural loading has errors, semantic validation is skipped to avoid duplicate noise from a partial model. Semantic diagnostics check references, empty trains or routes, timetable values, dwell times, platform references, incident targets, incident windows, base time, and repeated-service headways.

Save is allowed with semantic errors so students can preserve work in progress. Run is not allowed with any `SceneSeverity::Error`. Warnings and info diagnostics stay visible but do not block Run. Export diagnostics from `SceneExporter` are appended to the panel; export errors also block Run.

## Simulation handoff mechanics

Run uses the current `SceneModel`, including unsaved editor changes. The handoff creates a `QTemporaryDir` owned by the main window for the full run lifetime. Because `SceneExporter` reads a scene directory from disk, Run first serializes the current model into a scene subfolder of the staging directory through the same code path as Save, then exports that subfolder. `SceneExporter` writes legacy simulator input files into the staging directory, including passthrough data copied from `legacy/`.

The staging path must be absolute. Before simulator setup starts, `InputMainFolder` is set to the absolute `QTemporaryDir` path. This avoids the macOS GUI behavior that prepends the `.app` bundle `Resources` directory to relative input paths.

Simulation parameters are derived from the scene and staged input:

- `numTrackLines`: count of staged `TrackLines/B*` directories copied from scene `legacy/TrackLines/B*` data.
- `N_Routes`: number of routes in `signalling.json`.
- `startingSimulationTime`: `scene.json` `base_time`, converted from `HH:MM:SS` to seconds since midnight. `base_time` is optional in the schema; when it is missing the run uses `00:00:00` and the validation panel shows a warning.
- `times`: last scheduled service arrival plus a margin. The user can override this run duration before Run.

After these values are set, the existing sequence runs in the current main window:

```text
teardownGUI
simulation.resetState
simulation.setupEgtrain
simulation.prepareSimulation
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

Two edit fields depend on scene-layer work that is tracked separately from the editor issues: per-service performance values need a new field in `services.json` and `SceneModel`, and incidents take simulation effect only once `SceneExporter` exports `incidents.json` to the legacy input. Until then the incidents pane edits and saves scene data without changing run behavior.

## Explicit out-of-scope list

- Full infrastructure editing in V1.
- Drawing or editing nodes, arcs, tracks, switches, gradients, curves, speed limits, station topology, platform topology, signals, block sections, routes, or corridors.
- Treating legacy files as the editable source of truth.
- Editing files inside `legacy/` from the UI.
- Treating the temporary export directory as a scene directory.
- Replacing existing diagram windows or diagram export behavior as part of the scene editor flow.
- General-purpose JSON text editing inside the application.

## Consequences for #23 and #24

#23 implements the scene load and save UI:

- File menu actions: `Open Scene...`, `Save Scene`, `Save Scene As...`, recent-scenes submenu, and `Load Legacy Case...`.
- Main-window scene state: current scene directory, current `SceneModel`, dirty flag, recent-scenes `QSettings`, and action enabled state.
- Open dialog for scene directories.
- Save and Save As behavior for canonical JSON files while preserving `legacy/` passthrough data.
- Unsaved-change prompts when opening another scene, loading a legacy case, or quitting.

#24 implements the validation panel:

- Dockable `Scene Validation` panel.
- Table columns for severity, code, message, file, path, and suggested fix.
- Population from `SceneDiagnostic` entries on open, edit, save, and pre-run validation.
- Error summary in the status bar.
- Run gating when any diagnostic has error severity.
- Selection behavior that opens or focuses the related editor row when the diagnostic identifies an item.
