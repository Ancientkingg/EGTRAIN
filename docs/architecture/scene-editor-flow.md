# Scene Editor Flow

## Purpose and users

The scene editor gives instructors one window flow for creating a railway case,
authoring its canonical data, running EGTRAIN, and packaging the result. Students
can edit the supplied timetable and scenarios and inspect the same result views.
Researchers retain readable JSON inputs and reproducible native runs without
using legacy files.

## Window flow

- Start state: a scene is already loaded when the window first shows. `app/main.cpp` loads and validates the scene that the startup options name (Netherlands by default) and opens it in the window. Unless a startup option such as `-n` or `--scene` skips it, the window then shows the case chooser **Open a Case** over that scene. [Startup of the window](../guides/command-line.md#startup-of-the-window) lists the options. The chooser lists **Bundled cases** (scene folders and `.egscene` files in a `Scenes` folder in the working directory or next to the executable, and in `../Resources/Scenes` and `../share/EGTRAIN/Scenes` relative to the executable) and **Recent cases**. **Other ways to open...** offers `New Case Study...`, `Open Scene Folder...` and `Import Legacy Case...`. **Continue** keeps the loaded scene and **Open** opens the selected case. The toolbar button **Open Case** shows the same chooser later. A window without a scene exists only in the creator acceptance smoke test (`tools/e2e/creator_acceptance_smoke.sh` sets `QEGTRAIN_E2E_CREATOR_ACCEPTANCE`). There Save, the scene edit panes, and scene Run are disabled until a scene is created or opened.
- New scene: `New Case Study...` creates the smallest structurally valid `SceneModel`. It remains editable and saveable while semantic diagnostics identify the railway data still required for Run.
- Open scene: `Open Case Study...` selects an `.egscene` bundle; `Open Scene Folder...` selects an editable canonical directory. Both load the same canonical JSON into `SceneModel` and add the selected path to the recent-scenes list in `QSettings`. Normal mode opens quietly; `View > Advanced / Developer details` persists the choice and exposes the automatic Loaded Data and full validation detail. The existing Loaded Data and Validation View actions remain explicit in either mode. File, recent-scene, drop, and chooser opens resolve focused edits through one Save, Discard, or Cancel decision after the target is selected. Cancelled pickers and failed loads retain the current scene; teardown starts only after the incoming scene loads successfully.
- Compatibility: opening probes schema, bundle, and descriptive saved-with metadata before loading. Current scenes open directly; older scenes offer only an explicit upgrade copy when a registered migration reaches the current format, and are otherwise reported as unsupported and not opened (no migration is registered today); newer scenes offer **Check for Updates...** and Cancel. Automated/headless flows do not show these dialogs or start network checks. [Compatibility boundary](scene-model.md#compatibility-boundary) lists which scenes open.
- Validation: opening a scene populates the validation panel with `SceneDiagnostic` entries. Structural errors are shown first. Semantic validation runs only when structural loading has no errors; the full table remains available from the View menu and Loaded Data diagnostics. Normal mode keeps actionable errors and readiness visible while omitting automatic non-blocking warning counts; advanced mode also shows the summary counts and technical inventory.
- Edit panes: after a valid enough model loads, the editor panes show scene data from `SceneModel`. V1 edits update `SceneModel`; they do not edit legacy files directly.
- Save: `Save Scene` writes back to the opened bundle or directory. `Save Case Study As...` writes a portable `.egscene`; `Save Scene As Folder...` writes canonical JSON to a directory.
- Run handoff: Run revalidates the current model. Error diagnostics block Run, and so does a run with no selected service occurrence. If validation passes, the review window **Run simulation** opens. It names the scene and the scenario and shows the selected services, the start clock, the duration and the active incidents. Only its button **Run simulation** goes on: the shared native setup then builds the existing runtime globals directly from the model.
- Back to results: after the run, students can inspect speed, time, applied tractive effort, blocking-time, timetable, delay, and capacity results. Existing tables and diagrams provide CSV or PNG export where applicable. Run Results keeps the completed-run identity and, when relevant, the delay baseline identity beside the baseline/compare actions; the adjacent message states the next valid action or why a control is disabled. A valid comparison with no positive additional final-arrival delay reports that zero result explicitly.

`Load Legacy Case...` is an explicit conversion action. It reads a selected
external legacy directory into a new canonical scene; the source files are not
an editable or runtime fallback. After the source and destination are
validated, pending edits are resolved before conversion and the imported scene
opens without a second prompt.

## Main window layout

| Area | Contents | Enabled state |
|---|---|---|
| File menu | `New Case Study...`, `Open Case Study...`, `Open Scene Folder...`, `Save Scene`, `Save Case Study As...`, `Save Scene As Folder...`, `Run Scene`, `Recent Scenes`, `Load Legacy Case...`, `Quit`, `Set Output Folder...` | New, Open, Open Scene Folder, the legacy case picker, and Quit are always enabled. An open, New or legacy import during a run waits until the run has stopped ([Close, New and Open during a run](../development/build-and-test.md#close-new-and-open-during-a-run)). Recent Scenes is enabled when the list has entries. Save is enabled when a scene is loaded. Both Save As actions are enabled when a scene is loaded. Run Scene follows Run. Set Output Folder is disabled while a run is active. |
| Simulation menu and toolbar | `Run`, `Pause`, `Stop`, speed control | Run is enabled when a scene is loaded, no run is active and no scene error diagnostics are current. Pause and Stop are enabled only while simulation is running. |
| Central view | Existing network view and progress bar | Shows canonical scene infrastructure after open and simulation state after setup and run. |
| Editor docks | Case settings, infrastructure, rolling stock units, compositions, services and timetable, incidents, passengers | Enabled for a new or opened canonical scene. The Infrastructure, Services and Passengers docks are disabled while a run is active. A legacy case must first be imported as a scene. |
| Validation panel | Dockable table of diagnostics | Available from View in every mode. Updated on open, edit, save, and pre-run validation; automatic summary detail is richer in Advanced mode. |
| Loaded Data panel | Case/source metadata, parsed category counts, scenarios, provenance, validation status, editor links, runtime and result readiness | Automatically raised after scene open only in Advanced mode; the existing View action remains explicit. Item activation reuses the existing network view, validation table, and domain editors. |
| Existing info dock | Read-only selected item details for nodes, stations, arcs, connections, signals, trains | Enabled when the network view has selectable items. It stays read-only outside explicit editor controls. |
| Status bar | Transient messages, for example `Scene loaded: <name> (N services, M routes)`, `Running scene: <name>` and `Simulation complete - open the Diagrams menu for results`; three permanent fields: the follow status of the selected train, the validation summary and the zoom of the network view | Always visible. The scene name and the unsaved mark are in the window title. Normal mode calls out actionable validation errors; Advanced mode also shows warning/info counts. |

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
`DispatchController::prepareScene` clears the previous runtime state, checks
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
fails. `Failed` also covers a run that ends without results and without a stop
request, and a run whose scene changed while it ran. Completed simulation output
is reported as available, but remains outside canonical input.

## V1 edit panes

Editing uses explicit fields, table cells, and add, duplicate, move, and delete
controls. The network view previews canonical infrastructure; it is not a CAD
surface.

| Pane | Editable in V1 | Derived or read-only |
|---|---|---|
| Case settings | Name, description, base time, duration, buffer, recovery | Schema version, units, scene path, validation summary |
| Infrastructure | Tracks, nodes, arcs, blocks, connections, stations, platforms and platform geometry, signals, signalling areas, routes, dependencies, single-track restrictions, station boundaries | Network geometry preview and runtime diagnostics |
| Rolling stock units | ID; engine section with traction source, rows and plot; characteristics section with nine native physical fields and parameter source; session-only Link/Unlink/Retry controls for physical and traction files | Static traction plot; composition usage; live link status |
| Compositions | ID and ordered rolling-stock-unit membership | Selected-unit source references and traction plot |
| Services and timetable | ID, operating code, composition, route, entry time, performance, optional speed cap, repeat count/headway/code step, run selection, ordered stop matrix and stop dialog | Generated occurrence identities and offsets |
| Incidents | Scenario metadata; signal failures, train breakdowns, and entrance delays; targets, windows, occurrence, reduced speed, recovery, destination termination | Target choices derived from signals, blocks, routes, services, and timetable stops |
| Passengers | Passenger IDs; journeys, absolute time windows, and station endpoints; ordered service-occurrence legs; append import from the exact DAS and RouteChoice file pair | Row-specific import outcomes and validation diagnostics |

The signalling area table of the Infrastructure pane shows the level as a number
from 0 to 5. [Signalling levels](signalling-levels.md) names each level and says
what it does.

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
