# Scene Editor Flow

## Purpose and users

The scene editor gives instructors one window flow for creating a railway case,
authoring its canonical data, running EGTRAIN, and packaging the result. Students
can edit the supplied timetable and scenarios and inspect the same result views.
Researchers retain readable JSON inputs and reproducible native runs without
using legacy files.

## Window flow

- Start state: a scene is already loaded when the window first shows. `app/main.cpp` loads and validates the scene that the startup options name (Netherlands by default) and opens it in the window. Unless a startup option such as `-n` or `--scene`, or a `.egscene` file name on the command line that opens, skips it, the window then shows the case chooser **Open a Case** over that scene. The recovery prompt comes first when an earlier run left unsaved edits that can be recovered (Recovery at the start, below). A file name that does not open is reported and leaves the usual start: the default scene loads and the chooser is shown. [Startup of the window](../guides/command-line.md#startup-of-the-window) names the options that skip it. The chooser lists **Bundled cases** (scene folders and `.egscene` files in a `Scenes` folder in the working directory or next to the executable, and in `../Resources/Scenes` and `../share/EGTRAIN/Scenes` relative to the executable) and **Recent cases**. **Other ways to open...** offers `New Case Study...`, `Open Scene Folder...` and `Import Legacy Case...`. **Continue** keeps the loaded scene and **Open** opens the selected case. The toolbar button **Open Case** shows the same chooser later. A window without a scene exists only in the creator acceptance smoke test (`tools/e2e/creator_acceptance_smoke.sh` sets `QEGTRAIN_E2E_CREATOR_ACCEPTANCE`). There Save, the scene edit panes, and scene Run are disabled until a scene is created or opened.
- New scene: `New Case Study...` creates the smallest structurally valid [`SceneModel`]. It remains editable and saveable while semantic diagnostics identify the railway data still required for Run.
- Open scene: `Open Case Study...` selects an `.egscene` bundle; `Open Scene Folder...` selects an editable canonical directory. Both load the same canonical JSON into `SceneModel` and add the selected path to the recent-scenes list in `QSettings`. A `.egscene` file name on the command line is loaded and validated like the picker's choice and added to the recent scenes. No unsaved scene exists at that moment, and a failed open continues with the usual start. A bundle of an older or newer format is reported at once, without the upgrade copy or update check that the picker offers. Normal mode opens quietly; `View > Advanced / Developer details` persists the choice and exposes the automatic Loaded Data and full validation detail. The existing Loaded Data and Validation View actions remain explicit in either mode. File, recent-scene, drop, and chooser opens resolve focused edits through one Save, Discard, or Cancel decision after the target is selected. Cancelled pickers and failed loads retain the current scene; teardown starts only after the incoming scene loads successfully.
- Compatibility: opening probes schema, bundle, and descriptive saved-with metadata before loading. Current scenes open directly; older scenes offer only an explicit upgrade copy when a registered migration reaches the current format, and are otherwise reported as unsupported and not opened (no migration is registered today); newer scenes offer **Check for Updates...** and Cancel. Automated/headless flows do not show these dialogs or start network checks. [Compatibility boundary](scene-model.md#compatibility-boundary) lists which scenes open.
- Validation: opening a scene populates the validation panel with [`SceneDiagnostic`] entries. Structural errors are shown first. Semantic validation runs only when structural loading has no errors; the full table remains available from the View menu and Loaded Data diagnostics. Normal mode keeps actionable errors and readiness visible while omitting automatic non-blocking warning counts; advanced mode also shows the summary counts and technical inventory.
- Edit panes: after a valid enough model loads, the editor panes show scene data from `SceneModel`. V1 edits update `SceneModel`; they do not edit legacy files directly. The window holds the model in a [`SceneDocument`], which keeps the unsaved-changes state and counts every change; an editor changes the model in place and then calls `markChanged` ([Editor panels](#editor-panels)).
- Undo and Redo: the Edit menu holds `Undo` and `Redo` with the standard keys of the platform (Ctrl+Z on Windows and Linux with Ctrl+Y or Ctrl+Shift+Z for Redo, Cmd+Z and Cmd+Shift+Z on macOS). They also work while an editor dock floats as a window of its own. The window keeps the scene as it was after each committed edit: a field whose editing has finished, a choice, a toggle, an add, duplicate, delete or move, an accepted dialog. One such edit is one step. Text that is still being typed and a dialog that is cancelled add no step. The menu names the step, for example `Undo Delete service`, and `Undo Edit` when the change is not one part added or removed. A new edit after Undo discards the steps that Redo would repeat. The history keeps at most 100 steps and an estimated 128 MiB of snapshots, drops the oldest first, and lasts for the session ([Edit history](scene-model.md#edit-history)). Undo and Redo put back every authored part of the scene, so identifiers, references and the order of lists return as they were, and every editor shows the result. The loaded-data summary, the list of source files and the version that saved the scene come from the files and the checks, so they stay as they are. Validation runs again, and run results, a replay and the delay baseline are cleared as after any edit; Undo does not bring them back. Both actions are disabled while a run uses the scene. A text that is still being typed in an editor is committed as an edit first, and Undo then reverts it. While a text field or a number field has the focus it takes the shortcut for its own text, so use the menu or leave the field first.
- Unsaved mark and history: the scene is clean at the state that was opened or saved, and unsaved at every other state. Undo past that state and Redo away from it mark the scene unsaved again, and Save keeps the history. When the saved state has left the history, by a new edit after Undo or by the limits, the scene stays unsaved until the next save. Reverting a value by hand is two edits and leaves the scene unsaved. Opening, creating or importing a scene starts a new history once the new scene is in place; an open that fails leaves the history of the current scene.
- Not undone: opening, creating and saving a scene, the links of rolling stock units to their source files (a link stays with a unit that keeps its identifier and goes with one that is gone), the selected scenario, the generated service occurrences left out of a run, run results and replays, diagrams, the result rows of a passenger import, the selection in lists and the layout of the window.
- Autosave: while a scene has unsaved edits, the window keeps a recovery copy of it in a folder of its own below `Recovery` in the local application data folder of the user (`QEGTRAIN_RECOVERY_DIR` names another folder). The first edit after a write starts a timer of 30 seconds (`QEGTRAIN_RECOVERY_INTERVAL_MS` sets it in milliseconds). When the timer ends, a worker thread writes the newest state once, as an `.egscene` bundle with the writer that Save uses, so the window stays usable while a large scene is written. Later edits do not move the timer, and a state that is on disk is not written again. A new case that was never saved is copied the same way. The copy holds the authored scene and a small record: the name, the time, and the source, which is a bundle, a folder or a new case, with the absolute path of the file and the size and modification time that it had when the scene was opened or saved. Other files in a scene folder are not part of the copy. Autosave never writes to the file or folder that the scene came from, never marks the scene as saved, never changes the history, shows no dialog and sends nothing; the usage events do not see it ([Recovery files](../telemetry/README.md#recovery-files)). Every window keeps one copy, in a folder that only the user can read. A second window of EGTRAIN keeps its own, and the copy of a window that runs is never offered.
- Lifetime of a copy: the copy goes when the scene is saved with Save or Save As, when Undo returns to the saved state, when New or an open that succeeds replaces the scene, when the window closes after Save or Discard, and when the user discards it in the recovery prompt. An open that fails after Discard leaves the scene in the window and its copy where it was, and Cancel keeps the copy. A copy that a crash or a power loss left behind stays until the user decides; no age removes it. The application keeps at most 20 such copies, removes the oldest beyond that and says so in the prompt. A copy is a bundle, so the limits of a saved bundle apply to it: 16 MiB for one entry and 64 MiB in all. If the folder cannot be made or written, for example because it is read-only or the disk is full, the status bar says `Autosave unavailable` once for that scene, and the message goes when a copy is written again. It says why in words: a case with an incomplete entry, such as an incident without a target or a new service without a composition, cannot be written as a copy, and the message names the entry by its list and position (`incident 2`, `service 1`) and not the diagnostic code. Editing, saving and runs go on, and nothing is marked as saved.
- Recovery at the start: an interactive start looks for the copies that earlier runs left, after the window shows and before the case chooser. If at least one can be opened, **Recover Unsaved Work** lists them, newest first. A row names the case, the time of the copy and its source, which is the path or `New case study, not saved`, and says when the original changed after the copy was made, is newer than the copy, is missing or could not be compared. **Recover** opens the selected copy and is the default button unless the original may hold newer work, in which case **Decide Later** is. **Discard** asks for a second click that names the case, and it is the only way to delete a copy that can be opened. **Decide Later**, Escape and closing the dialog keep every copy for the next start. A copy that a newer version or another scene version made is listed and can only be discarded. After Recover the chooser stays closed. Scripted, offscreen and headless runs neither keep nor offer copies unless `QEGTRAIN_RECOVERY_DIR` is set, which tests do for a folder of their own.
- Recovered scene: Recover opens the copy as an unsaved scene without a file, so it has no saved state to return to. Save leads to `Save Case Study As...`, whose dialog starts at the file that the copy came from. The original is written only when the user chooses it there and confirms the overwrite. The recovered scene is not added to the recent scenes and is not counted as an opened scene in the usage events. Its copy moves to the folder of the window, so a crash right after the recovery loses nothing, and a later copy still names the original, so a second prompt can tell that it changed.
- Save: `Save Scene` writes back to the opened bundle or directory. `Save Case Study As...` writes a portable `.egscene`; `Save Scene As Folder...` writes canonical JSON to a directory.
- Run handoff: Run revalidates the current model. Error diagnostics block Run, and so does a run with no selected service occurrence. If validation passes, the review window **Run simulation** opens. It names the scene and the scenario and shows the selected services, the start clock, the duration and the active incidents. Only its button **Run simulation** goes on: the shared native setup then builds the run directly from the model.
- Back to results: after the run, students can inspect speed, time, applied tractive effort, blocking-time, timetable, delay, and capacity results. Existing tables and diagrams provide CSV or PNG export where applicable. Run Results keeps the completed-run identity and, when relevant, the delay baseline identity beside the baseline/compare actions; the adjacent message states the next valid action or why a control is disabled. Compare delays opens the Delay analysis window on its Against baseline tab, which states a zero positive additional delay and the number of unchanged occurrences explicitly.

`Load Legacy Case...` is an explicit conversion action. It reads a selected
external legacy directory into a new canonical scene; the source files are not
an editable or runtime fallback. After the source and destination are
validated, pending edits are resolved before conversion and the imported scene
opens without a second prompt.

## Main window layout

| Area | Contents | Enabled state |
|---|---|---|
| File menu | `New Case Study...`, `Open Case Study...`, `Open Scene Folder...`, `Save Scene`, `Save Case Study As...`, `Save Scene As Folder...`, `Run Scene`, `Recent Scenes`, `Load Legacy Case...`, `Set Output Folder...`, `Quit` | New, Open, Open Scene Folder, the legacy case picker, and Quit are always enabled. An open or New during a run waits until the run has stopped, and so does the opening of an imported legacy case; the import itself, which writes the destination scene, runs at once ([Close, New and Open during a run](../development/build-and-test.md#close-new-and-open-during-a-run)). Recent Scenes is enabled when the list has entries. Save is enabled when a scene is loaded. Both Save As actions are enabled when a scene is loaded. Run Scene follows Run. Set Output Folder is disabled while a run is active. |
| Edit menu | `Undo`, `Redo`, `Find...` | Undo and Redo are enabled when a scene is loaded, no run is active and the history has a step to go back or forward; the text names the step. Find is always enabled. |
| Simulation menu and toolbar | `Run`, `Pause`, `Stop`, speed control | Run is enabled when a scene is loaded, no run is active and no scene error diagnostics are current. Pause and Stop are enabled only while simulation is running. |
| Central view | Existing network view and progress bar | Shows canonical scene infrastructure after open and simulation state after setup and run. |
| Editor docks | Case settings, infrastructure, rolling stock units, compositions, services and timetable, incidents, passengers | Enabled for a new or opened canonical scene. The Infrastructure, Services and Passengers docks are disabled while a run is active. A legacy case must first be imported as a scene. |
| Validation panel | Dockable table of diagnostics | Available from View in every mode. Updated on open, edit, save, and pre-run validation; automatic summary detail is richer in Advanced mode. |
| Loaded Data panel | Case/source metadata, parsed category counts, scenarios, provenance, validation status, editor links, runtime and result readiness | Automatically raised after scene open only in Advanced mode; the existing View action remains explicit. Item activation reuses the existing network view, validation table, and domain editors. |
| Existing info dock (the inspector) | Read-only selected item details for nodes, stations, arcs, connections, signals, trains. The title is `Preview: <kind> <id> (not running)` before a run and Node Info, Station Node Info, Arc Info, Connection Info, Signalling Info or Train Info during one. | Enabled when the network view has selectable items. It stays read-only outside explicit editor controls. It closes when a run stops or its results are discarded. |
| Status bar | Transient messages, for example `Scene loaded: <name> (N services, M routes)`, `Running scene: <name>` and `Simulation complete - results in the Diagrams menu`; three permanent fields: the follow status of the selected train, the validation summary and the zoom of the network view | Always visible. The scene name and the unsaved mark are in the window title. Normal mode calls out actionable validation errors; Advanced mode also shows warning/info counts. |

## Validation lifecycle

Validation runs when a bundle or directory opens, after each committed editor change, after Save or Save As, and immediately before Run. The pre-run validation is mandatory even when the panel already shows no errors.

Structural diagnostics come from the bundle reader or directory loader and the required JSON files. If structural loading has errors, semantic validation is skipped to avoid duplicate noise from a partial model. Semantic diagnostics check topology, identifiers and references, rolling-stock values and traction intervals, timetable values, signalling coverage, incidents, base time, and repeated-service rules.

Save is allowed with semantic errors so students can preserve work in progress. Run is not allowed with any `SceneSeverity::Error`. Warnings and info diagnostics remain available in the full Validation table but do not block Run; normal-mode automatic presentation omits their non-blocking summary counts. Native builder diagnostics appear under **Runtime and results** in Loaded Data, and builder errors block Run.

The Advanced / Developer details choice changes presentation only. It does not
commit focused editor values, replace the full diagnostic table, change run
gating, or change the scene/result model. The scenario library retains incident
and entrance-delay counts in normal mode; an invalid scenario remains marked
`Invalid`, while non-blocking `Warning` detail is reserved for Advanced mode.

## Simulation handoff mechanics

Run uses the current `SceneModel`, including unsaved editor changes.
[`DispatchController::prepareScene`] clears the previous runtime state, checks
that the scene is runnable, invokes the infrastructure/signalling builder, then
the operations builder, and prepares the configured output directory. An error
found before the output directory is prepared resets the runtime state. It does
not serialize, export, or stage a second input representation.

Simulation parameters are canonical:

- `numTrackLines`: number of entries in `infrastructure.json.tracks`.
- `N_Routes`: number of routes in `signalling.json`.
- `startingSimulationTime`: `scene.json.base_time`, converted from `HH:MM:SS`.
- `times`: `scene.json.simulation_settings.duration_seconds`, or the value of `-h`
  when that option is given (see [Options](../guides/command-line.md#options)).

`DispatchController::prepareScene` sets these values from the scene. Run calls
it in the current main window with this sequence:

```text
teardownGUI
simulation.prepareScene(SceneModel)
setupGUI
```

Only after setup succeeds does the simulation worker start. Opening or creating
a scene calls `teardownGUI` and `simulation.resetState` before it replaces the
current scene. Case replacement waits for the old worker to stop and ignores any
queued completion from that worker. Disabled scenario controls are not pending
edits.

The Loaded Data panel reports one global runtime state instead of claiming each
input file built a separate runtime model. It is `Not built` after open or an
edit, `Ready` after `prepareScene` succeeds, `Running` while the worker runs,
`Completed` when a run ends with results, `Stopped` when a stop request ends a
run before it completes, and `Failed` with builder diagnostics after preparation
fails. `Failed` also covers a run that ends without results, unless a stop
request ended it before it completed, and a run whose scene changed while it ran. Completed simulation output
is reported as available, but remains outside canonical input.

## Editor panels

Each editor dock holds one panel from `editors/`, a [`SceneEditorPanel`]. A panel shows the model of the window's `SceneDocument`, changes it in place and then calls `markChanged` with the parts that it changed and a label. It also emits `changed`, which the window answers by running validation again. The window creates the docks, gives every panel the document, and connects the panels with each other and with the run. It holds no editing code of its own.

`SceneDocument` owns the `SceneModel`. It knows whether a scene is loaded, whether it differs from what was saved, whether a run lets it be edited, and how often it has changed. Its signal `changed` names the parts that changed, `replaced` follows the opening or creating of a scene, and `dirtyChanged` and `editableChanged` follow the two flags. The window answers `changed` by adding an Undo step and by clearing the run results and the delay baseline. A panel that shows data derived from another part of the scene, such as the targets of an incident, refreshes itself from the same signal. Undo and Redo put an earlier state back with `restore`, which emits no `changed`, so the window refreshes the panels itself.

Saving, the question about unsaved changes, validation and Undo first take the text of every field that is still being typed: the window calls `commitPendingEditors` on each panel in turn. While a run uses the scene the document is not editable, and the service, scenario, incident and entrance delay panels take nothing.

The Infrastructure panel lists one kind of entity at a time in a table. Each kind is an [`InfrastructureFacet`] in `editors/infrastructure/` that knows its columns and rows and what an edited cell, an addition and a deletion do to the model. `TopologyFacets` holds tracks, nodes, arcs and connections, `BlockRouteFacets` blocks, routes, block dependencies and single-track restrictions, `StationFacets` stations, platforms and station boundaries, and `SignalFacets` signals and signalling areas. Renaming an entity rewrites what refers to it (`scene/SceneReferences`), and an entity that others use cannot be deleted.

The facet combo is on top of the panel, and the buttons that add and delete an entity are in a row under the table, with the buttons that insert and move a block. The sections of a route are listed under the heading "Route sections" below the table, in a splitter that the user can drag. The sentence above the table comes from `infrastructureGuidanceText`, which has a sentence of its own for each of the thirteen kinds when its table is empty. It names a prerequisite (a track, two nodes, a station) only while the scene lacks it, says that the scene cannot be edited while a run is in progress when Add is off, and asks for a case when none is open.

The Validation and Loaded Data docks hold the table and the tree of a [`ValidationPanel`], which is not an editor. Its `refresh` validates the scene of the document, writes the diagnostics to the list that the window and the scenario and passenger panels read, fills the table, the validation summary of the status bar and the tree, and emits `refreshed`, which the window answers by refreshing the scenario and passenger panels and the actions. The window gives the panel the duration of the next run, the Advanced details choice, and what the tree shows of the scene file and the run. A row of the tree that stands for a scene item emits `targetActivated`, and the window opens the editor or the view.

Each panel has a widget test, and each group of facets one. The editor smoke and the creator acceptance smoke run the panels in the whole window ([Editor Smoke Test](../development/build-and-test.md#editor-smoke-test)).

## V1 edit panes

Editing uses explicit fields, table cells, and add, duplicate, move, and delete
controls. The network view previews canonical infrastructure; it is not a CAD
surface.

| Pane | Editable in V1 | Derived or read-only |
|---|---|---|
| Case settings | Name, description, base time, duration, buffer, recovery | Schema version, units, scene path, validation summary |
| Infrastructure | Tracks, nodes, arcs, blocks, connections, stations, platforms and platform geometry, signals, signalling areas, routes, dependencies, single-track restrictions, station boundaries | Network geometry preview and runtime diagnostics |
| Rolling stock units | ID; Traction tab with the engine section (traction source, rows and plot); Physical tab with the characteristics section (nine native physical fields and parameter source); session-only Link/Unlink/Retry controls for physical and traction files | Static traction plot; composition usage; live link status |
| Compositions | ID and ordered rolling-stock-unit membership | Selected-unit source references and traction plot |
| Services and timetable | ID, operating code, composition, route, entry time, performance, optional speed cap, repeat count/headway/code step, run selection, ordered stop matrix and stop dialog | Generated occurrence identities and offsets |
| Incidents | Scenario metadata; signal failures, train breakdowns, and entrance delays; targets, windows, occurrence, reduced speed, recovery, destination termination | Target choices derived from signals, blocks, routes, services, and timetable stops |
| Passengers | Passenger IDs; journeys, absolute time windows, and station endpoints; ordered service-occurrence legs; append import from the exact DAS and RouteChoice file pair | Row-specific import outcomes and validation diagnostics; errors of the selection in one callout and warnings and notes in a second |

The signalling area table of the Infrastructure pane has a list for the
signalling system and a list for the track. The system list holds the six levels
with the names of [Signalling levels](../guides/signalling-levels.md), which also says what
each level does. The track list holds "(all tracks)" and each track of the scene
with the extent of its blocks. A value that does not exist is listed first as
"Invalid level N" or "Invalid track: X". An area whose system is not chosen lists
"Choose a signalling system" first. A new area starts with the extent of the
network and no system, and the case cannot run until a system is chosen.

Rolling-stock links are UI session state, not scene schema. The watcher keeps an
absolute selected file and its parent directory armed through atomic replacement,
deletion, and recreation; a short debounce groups callbacks for one file. A
complete validated candidate is compared with the last accepted values before
the canonical unit changes. Shared links use one grouped Keep local/Reload
decision; a focused local edit prompts for a conflict decision, while an update
observed during a run is deferred and offered after completion. New scenes,
successful opens, unlink, deletion, and relinking clear
the corresponding observers; failed or cancelled opens leave the current links
intact. Saved scenes therefore remain portable when their source files are
absent.

The scenario editor can create and delete non-default scenarios and edit the
canonical entrance-delay rows used by native staging.

Services show their service code, configured total and count within the
simulation period separately. Selected-in-period counts exclude unchecked
generated services. Maximum speed restriction uses six significant display
digits; focusing or saving an untouched value preserves its stored precision.
Running performance is applied when editing of the box ends (Enter, Tab or
leaving the box), not for each digit typed; Save and Run apply a value that is
still being typed.
The Category chooser edits only service metadata. It offers six presets and No
category; an imported unknown value is shown explicitly and retained until
changed. The Service visualization colour control follows it: a swatch, Choose...
(a Qt colour dialog without alpha, written as lower-case `#rrggbb`) and Default,
which removes the value. A stored text that is not `#RRGGBB` is shown as
"Invalid: <text>" and retained until changed. Both edits mark the scene
modified. Duplication copies the category and the colour with the other service
settings. The window resolves a train's colour from the service id that the
simulation snapshot carries, so every occurrence of a service shares it.
Route choices show endpoints and traversal direction, with canonical IDs as
secondary labels and item data. Tooltips list traversed stations, not scheduled
calls. The dialog for a new stop has a position chooser whose item data is the
insertion index, so a repeated station gives distinct entries. Stop choices are the
route visits between the neighbours of that position
(`sceneStopInsertionWindow`); a position without a window shows the reason, offers
no station and blocks Accept. Accept calls `insertSceneStop`, which inserts only
when the new stop resolves and no resolved stop changes its visit, and otherwise
leaves the service unchanged. Add Stop opens after the last stop or at the last
position that has choices and shows a message only when no position has any;
Insert After Selected opens after the selected row. Editing a stop keeps the
remaining-route choices. Existing invalid assignments remain visible and
saveable, with a reason beside the platform selector. Route edits never delete
or silently retarget stops.

Clicking or keyboard-activating a timetable row opens a copied-stop dialog.
Accept validates the focused fields and commits once; Cancel changes nothing,
including when adding a stop. The matrix and dialog offer Elapsed/Clock time
with the canonical case base time shown. Blank arrival or departure means absent,
not zero. Clock input uses `HH:MM:SS[.fraction]` and explicit `+Nd ` prefixes for
later days; a clock before base is not silently interpreted as tomorrow.
Representation changes preserve stored offsets and untouched precision. The
dialog blocks malformed, nonfinite or newly negative input; chronology errors
can be saved as drafts but block Run. A short dwell window remains advisory.
An empty stop matrix is a service with no scheduled calls, with no extra switch.

Train incidents use one generated-service chooser, with code, route, scheduled
entry and secondary canonical identity. Selecting a row stores both service ID
and occurrence. Historical all-occurrence targets retain an explicit scope row;
removed targets remain visibly invalid until deliberately changed.

## Explicit out-of-scope list

- Drag-and-drop railway CAD, route painting, or automatic signal placement.
- Automatic timetable optimization or performance sweeps.
- A generic scenario scripting language.
- Treating legacy files as the editable source of truth.
- Editing legacy source files from the UI.
- Treating an explicit interoperability export as a scene directory.
- General-purpose JSON text editing inside the application.

[`SceneModel`]: https://ancientkingg.github.io/EGTRAIN/latest/api/struct_scene_model.html
[`SceneDiagnostic`]: https://ancientkingg.github.io/EGTRAIN/latest/api/struct_scene_diagnostic.html
[`SceneDocument`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_scene_document.html
[`DispatchController::prepareScene`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_dispatch_controller.html
[`SceneEditorPanel`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_scene_editor_panel.html
[`InfrastructureFacet`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_infrastructure_facet.html
[`ValidationPanel`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_validation_panel.html
