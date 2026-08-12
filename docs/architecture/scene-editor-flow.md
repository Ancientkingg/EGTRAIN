# Scene Editor Flow

## Purpose and users

The scene editor gives instructors one window flow for creating a railway case,
authoring its canonical data, running EGTRAIN, and packaging the result. Students
can edit the supplied timetable and scenarios and inspect the same result views.
Researchers retain readable JSON inputs and reproducible native runs without
using legacy files.

## Window flow

- Start state: the main window opens with no scene selected. `New Case Study...`, `Open Case Study...`, `Open Scene Folder...`, recent scenes, `Load Legacy Case...`, and `Quit` are available. Save, scene edit panes, and scene Run are disabled until a scene opens.
- New scene: `New Case Study...` creates the smallest structurally valid `SceneModel`. It remains editable and saveable while semantic diagnostics identify the railway data still required for Run.
- Open scene: `Open Case Study...` selects an `.egscene` bundle; `Open Scene Folder...` selects an editable canonical directory. Both load the same canonical JSON into `SceneModel`, add the selected path to the recent-scenes list in `QSettings`, and raise the non-modal Loaded Data review.
- Validation: opening a scene populates the validation panel with `SceneDiagnostic` entries. Structural errors are shown first. Semantic validation runs only when structural loading has no errors; the panel remains available from the View menu and Loaded Data diagnostics.
- Edit panes: after a valid enough model loads, the editor panes show scene data from `SceneModel`. V1 edits update `SceneModel`; they do not edit legacy files directly.
- Save: `Save Scene` writes back to the opened bundle or directory. `Save Case Study As...` writes a portable `.egscene`; `Save Scene As Folder...` writes canonical JSON to a directory.
- Run handoff: Run revalidates the current model. Error diagnostics block Run. If validation passes, the shared native setup builds the existing runtime globals directly from that model.
- Back to results: after the run, students can inspect speed, time, applied tractive effort, blocking-time, timetable, delay, and capacity results. Existing tables and diagrams provide CSV or PNG export where applicable.

`Load Legacy Case...` is an explicit conversion action. It reads a selected
external legacy directory into a new canonical scene; the source files are not
an editable or runtime fallback.

## Main window layout

| Area | Contents | Enabled state |
|---|---|---|
| File menu | `Open Case Study...`, `Open Scene Folder...`, `Save Scene`, `Save Case Study As...`, `Save Scene As Folder...`, recent scenes, `Load Legacy Case...`, `Quit` | Open, recent scenes, legacy case picker, and Quit are always enabled. Save is enabled when a scene is loaded and dirty. Both Save As actions are enabled when a scene is loaded. |
| Simulation menu and toolbar | `Run`, `Pause`, `Stop`, speed control | Run is enabled when a runnable scene is loaded and no scene error diagnostics are current. Pause and Stop are enabled only while simulation is running. |
| Central view | Existing network view and progress bar | Empty at startup. Shows canonical scene infrastructure after open and simulation state after setup and run. |
| Editor docks | Case settings, infrastructure, train units, compositions, services and timetable, incidents | Enabled for a new or opened canonical scene. A legacy case must first be imported as a scene. |
| Validation panel | Dockable table of diagnostics | Visible after scene open. Updated on open, edit, save, and pre-run validation. |
| Loaded Data panel | Case/source metadata, parsed category counts, scenarios, provenance, validation status, editor links, runtime and result readiness | Raised after scene open. Item activation reuses the existing network view, validation table, and domain editors. |
| Existing info dock | Read-only selected item details for nodes, stations, arcs, connections, signals, trains | Enabled when the network view has selectable items. It stays read-only outside explicit editor controls. |
| Status bar | Current scene name, scene path, dirty state, validation summary, run state | Always visible. Scene-specific fields are empty before a scene opens. |

## Validation lifecycle

Validation runs when a bundle or directory opens, after each committed editor change, after Save or Save As, and immediately before Run. The pre-run validation is mandatory even when the panel already shows no errors.

Structural diagnostics come from the bundle reader or directory loader and the required JSON files. If structural loading has errors, semantic validation is skipped to avoid duplicate noise from a partial model. Semantic diagnostics check topology, identifiers and references, rolling-stock values and traction intervals, timetable values, signalling coverage, incidents, base time, and repeated-service rules.

Save is allowed with semantic errors so students can preserve work in progress. Run is not allowed with any `SceneSeverity::Error`. Warnings and info diagnostics stay visible but do not block Run. Native builder diagnostics appear under **Runtime and results** in Loaded Data, and builder errors block Run.

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

The Loaded Data panel reports one global runtime state instead of claiming each
input file built a separate runtime model. It is `Not built` after open or an
edit, `Ready` after `prepareScene` succeeds, and `Failed` with builder
diagnostics after preparation fails. Completed simulation output is reported as
available, but remains outside canonical input.

## V1 edit panes

Editing uses explicit fields, table cells, and add, duplicate, move, and delete
controls. The network view previews canonical infrastructure; it is not a CAD
surface.

| Pane | Editable in V1 | Derived or read-only |
|---|---|---|
| Case settings | Name, description, base time, duration, buffer, recovery | Schema version, units, scene path, validation summary |
| Infrastructure | Tracks, nodes, arcs, blocks, connections, stations, platforms and platform geometry, signals, signalling areas, routes, dependencies, single-track restrictions, station boundaries | Network geometry preview and runtime diagnostics |
| Train units | ID, nine native physical fields, traction rows, parameter source reference, traction source reference | Static traction plot; composition usage |
| Compositions | ID and ordered train-unit membership | Selected-unit source references and traction plot |
| Services and timetable | ID, operating code, composition, route, through state, entry time, performance, optional speed cap, repeat count/headway/code step, run selection, ordered stops, platforms, planned arrival/departure, dwell | Generated occurrence identities and offsets |
| Incidents | Scenario metadata; signal failures, train breakdowns, and entrance delays; targets, windows, occurrence, reduced speed, recovery, destination termination | Target choices derived from signals, blocks, routes, services, and timetable stops |
| Passengers | Passenger IDs; journeys, absolute time windows, and station endpoints; ordered service-occurrence legs; append import from the exact DAS and RouteChoice file pair | Row-specific import outcomes and validation diagnostics |

The scenario editor can create and delete non-default scenarios and edit the
canonical entrance-delay rows used by native staging.

## Explicit out-of-scope list

- Drag-and-drop railway CAD, route painting, or automatic signal placement.
- Automatic timetable optimization or performance sweeps.
- A generic scenario scripting language.
- Treating legacy files as the editable source of truth.
- Editing legacy source files from the UI.
- Treating an explicit interoperability export as a scene directory.
- General-purpose JSON text editing inside the application.
