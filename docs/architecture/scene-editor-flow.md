# Scene Editor Flow

## Purpose and users

The scene editor gives instructors one window flow for creating a railway case,
authoring its canonical data, running EGTRAIN, and packaging the result. Students
can edit the supplied timetable and scenarios and inspect the same result views.
Researchers retain readable JSON inputs and reproducible native runs without
using legacy files.

## Window flow

- Start state: a scene is already loaded when the window first shows. `app/main.cpp` loads and validates the scene that the startup options name (Netherlands by default) and opens it in the window. Unless a startup option such as `-n` or `--scene`, or a `.egscene` file name on the command line that opens, skips it, the window then shows the case chooser **Open a Case** over that scene. The recovery prompt comes first when an earlier run left unsaved edits that can be recovered (Recovery at the start, below). A file name that does not open is reported and leaves the usual start: the default scene loads and the chooser is shown. [Startup of the window](../guides/command-line.md#startup-of-the-window) names the options that skip it. The chooser lists **Bundled cases** (scene folders and `.egscene` files in a `Scenes` folder in the working directory or next to the executable, and in `../Resources/Scenes` and `../share/EGTRAIN/Scenes` relative to the executable) and **Recent cases**. **Other ways to open...** offers `New Case...`, `Open Case File...`, `Open Case Folder...` and `Import Legacy Case...`. **Continue** keeps the loaded scene and **Open** opens the selected case. `File > Open Case...` (Ctrl+O) and the toolbar button **Open Case**, which show one action, show the same chooser later. A window without a scene exists only in the creator acceptance smoke test (`tools/e2e/creator_acceptance_smoke.sh` sets `QEGTRAIN_E2E_CREATOR_ACCEPTANCE`). There Save, the scene edit panes, and scene Run are disabled until a scene is created or opened.
- New scene: `New Case...` creates the smallest structurally valid [`SceneModel`]. It remains editable and saveable while semantic diagnostics identify the railway data still required for Run.
- Open scene: `Open Case File...` in the chooser selects an `.egscene` bundle; `Open Case Folder...` selects an editable canonical directory. Both load the same canonical JSON into `SceneModel` and add the selected path to the recent-scenes list in `QSettings`, unless it lies in a folder of installed scenes beside the executable (`isInstalledScenePath` in `app/SceneSavePolicy.h`). The chooser lists those under **Bundled cases**, and their folder can change at every start, as the mount folder of an AppImage does. A `.egscene` file name on the command line is loaded and validated like the picker's choice and added to the recent scenes in the same way. No unsaved scene exists at that moment, and a failed open continues with the usual start. A bundle of an older or newer format is reported at once, without the upgrade copy or update check that the picker offers. Normal mode opens quietly; `View > Advanced Details` persists the choice and exposes the automatic Loaded Data and full validation detail. The existing Loaded Data and Validation View actions remain explicit in either mode. File, recent-scene, drop, and chooser opens resolve focused edits through one Save, Discard, or Cancel decision after the target is selected. Cancelled pickers and failed loads retain the current scene; teardown starts only after the incoming scene loads successfully.
- Compatibility: opening probes schema, bundle, and descriptive saved-with metadata before loading. Current scenes open directly; older scenes offer only an explicit upgrade copy when a registered migration reaches the current format, and are otherwise reported as unsupported and not opened (no migration is registered today); newer scenes offer **Check for Updates...** and Cancel. Automated/headless flows do not show these dialogs or start network checks. [Compatibility boundary](scene-model.md#compatibility-boundary) lists which scenes open.
- Validation: opening a scene populates the validation panel with [`SceneDiagnostic`] entries. Structural errors are shown first. Semantic validation runs only when structural loading has no errors; the full table remains available from the View menu and Loaded Data diagnostics. Normal mode keeps actionable errors and readiness visible while omitting automatic non-blocking warning counts; advanced mode also shows the summary counts and technical inventory.
- Edit panes: after a valid enough model loads, the editor panes show scene data from `SceneModel`. V1 edits update `SceneModel`; they do not edit legacy files directly. The window holds the model in a [`SceneDocument`], which keeps the unsaved-changes state and counts every change; an editor changes the model with `edit` ([Editor panels](#editor-panels)).
- Undo and Redo: the Edit menu holds `Undo` and `Redo` with the standard keys of the platform (Ctrl+Z on Windows and Linux with Ctrl+Y or Ctrl+Shift+Z for Redo, Cmd+Z and Cmd+Shift+Z on macOS). They also work while an editor dock floats as a window of its own. The window keeps the scene as it was after each committed edit: a field whose editing has finished, a choice, a toggle, an add, duplicate, delete or move, an accepted dialog. One such edit is one step. Text that is still being typed and a dialog that is cancelled add no step. The menu names the step, for example `Undo Delete service`, and `Undo Edit` when the change is not one part added or removed. A new edit after Undo discards the steps that Redo would repeat. The history keeps at most 100 steps and an estimated 128 MiB of snapshots, drops the oldest first, and lasts for the session ([Edit history](scene-model.md#edit-history)). Undo and Redo put back every authored part of the scene, so identifiers, references and the order of lists return as they were, and every editor shows the result. The loaded-data summary, the list of source files and the version that saved the scene come from the files and the checks, so they stay as they are. Validation runs again, and run results, a replay and the delay baseline are cleared as after any edit; Undo does not bring them back. The network view keeps its zoom and its centre. Both actions are disabled while a run uses the scene. A text that is still being typed in an editor is committed as an edit first, and Undo then reverts it. While a text field or a number field has the focus it takes the shortcut for its own text, so use the menu or leave the field first.
- Unsaved mark and history: the scene is clean at the state that was opened or saved, and unsaved at every other state. Undo past that state and Redo away from it mark the scene unsaved again, and Save keeps the history. When the saved state has left the history, by a new edit after Undo or by the limits, the scene stays unsaved until the next save. Reverting a value by hand is two edits and leaves the scene unsaved. Opening, creating or importing a scene starts a new history once the new scene is in place; an open that fails leaves the history of the current scene.
- Run selection: choosing a scenario and leaving generated service occurrences out of a run, or taking them in, add no step. A service that Undo or Redo brings back, such as one whose delete or rename Undo reverts, has the same occurrences left out as when its state was last left, and the other services keep what is left out now. A scenario that Undo or Redo brings back is selected again when it was selected in that state. When the selected scenario is gone, the scenario selected in the restored state takes its place, or else the default scenario. The window keeps the selected scenario and the occurrences left out with the live state of the history before an edit (`aboutToEdit`) and before Undo or Redo leaves it.
- Rolling stock links: linking and unlinking a source file add no step, and taking a changed file into a unit adds one. A link stays with a unit that keeps its identifier and goes with one that is gone. A unit that Undo or Redo brings back, such as one whose delete or rename Undo reverts, gets the links to its parameter file and its traction file that it had when its state was last left, with their status. Its files are watched again, and a file that changed meanwhile is taken as any change of a linked file is. The window keeps the links with the live state of the history together with the run selection.
- Not undone: opening, creating and saving a scene, run results and replays, diagrams, the result rows of a passenger import, the selection in lists and the layout of the window.
- Autosave: while a scene has unsaved edits, the window keeps a recovery copy of it in a folder of its own below `Recovery` in the local application data folder of the user (`QEGTRAIN_RECOVERY_DIR` names another folder, and a test run with `QEGTRAIN_E2E_SETTINGS_DIR` uses `Recovery` in that folder). The first edit after a write starts a timer of 30 seconds (`QEGTRAIN_RECOVERY_INTERVAL_MS` sets it in milliseconds). When the timer ends, a worker thread writes the newest state once, as an `.egscene` bundle with the writer that Save uses, so the window stays usable while a large scene is written. Later edits do not move the timer, and a state that is on disk is not written again. A new case that was never saved is copied the same way. The copy holds the authored scene and a small record: the name, the time, and the source, which is a bundle, a folder or a new case, with the absolute path of the file and the size and modification time that it had when the scene was opened or saved. Other files in a scene folder are not part of the copy. Autosave never writes to the file or folder that the scene came from, never marks the scene as saved, never changes the history, shows no dialog and sends nothing; the usage events do not see it ([Recovery files](../telemetry/README.md#recovery-files)). Every window keeps one copy, in a folder that only the user can read. A second window of EGTRAIN keeps its own, and the copy of a window that runs is never offered.
- Lifetime of a copy: the copy goes when the scene is saved with Save or Save As, when Undo returns to the saved state, when New or an open that succeeds replaces the scene, when the window closes after Save or Discard, and when the user discards it in the recovery prompt. An open that fails after Discard leaves the scene in the window and its copy where it was, and Cancel keeps the copy. A copy that a crash or a power loss left behind stays until the user decides; no age removes it. The application keeps at most 20 such copies, removes the oldest beyond that and says so in the prompt. A copy is a bundle, so the limits of a saved bundle apply to it: 16 MiB for one entry and 64 MiB in all. If the folder cannot be made or written, for example because it is read-only or the disk is full, the status bar says `Autosave unavailable` once for that scene, and the message goes when a copy is written again. It says why in words: a case with an incomplete entry, such as an incident without a type, cannot be written as a copy, and the message names the entry by its list and position (`incident 2`) and not the diagnostic code. A reference that is not chosen yet, such as the target of a new incident or the composition of a new service, does not stop the copy. Editing, saving and runs go on, and nothing is marked as saved.
- Recovery at the start: an interactive start looks for the copies that earlier runs left, after the window shows and before the case chooser. If at least one can be opened, **Recover Unsaved Work** lists them, newest first. A row names the case, the time of the copy and its source, which is the path or `Not saved yet`, and says when the original changed after the copy was made, is newer than the copy, is missing or could not be compared. **Recover** opens the selected copy and is the default button unless the original may hold newer work, in which case **Decide Later** is. **Discard** asks for a second click that names the case, and it is the only way to delete a copy that can be opened. **Decide Later**, Escape and closing the dialog keep every copy for the next start. A copy that a newer version or another scene version made is listed and can only be discarded. After Recover the chooser stays closed. Scripted, offscreen and headless runs neither keep nor offer copies unless `QEGTRAIN_RECOVERY_DIR` is set, which tests do for a folder of their own.
- Recovered scene: Recover opens the copy as an unsaved scene without a file, so it has no saved state to return to. Save leads to the dialog of a new case file, as `Case File...` of `Save As...` does, and that dialog starts at the file that the copy came from, or, when that was a case folder, at a case file of the folder's name beside it. The original is written only when the user chooses it there and confirms the overwrite. The recovered scene is not added to the recent scenes and is not counted as an opened scene in the usage events. Its copy moves to the folder of the window, so a crash right after the recovery loses nothing, and a later copy still names the original, so a second prompt can tell that it changed.
- Save: `Save` writes back to the opened bundle or directory; a scene without a location goes to the dialog of a new `.egscene`. `Save As...` first asks, through `UserPrompts::chooseButton`, for the format: `Case File...` writes a portable `.egscene` and `Case Folder...` writes canonical JSON to a directory. The default answer is the format of the open scene, and `Case File...` for a bundle or a scene without a location; Cancel saves nothing.
- Run handoff: Run refreshes the validation of the current model, which validates it again when it changed since its last validation (Validation lifecycle, below). Error diagnostics block Run, and so does a run with no selected service occurrence. If validation passes, the review window **Run simulation** opens. It names the scene and the scenario and shows the selected services, the start clock, the duration and the active incidents. Only its button **Run simulation** goes on: the shared native setup then builds the run directly from the model.
- Back to results: after the run, students can inspect speed, time, applied tractive effort, blocking-time, timetable, delay, and capacity results. Existing tables and diagrams provide CSV or PNG export where applicable. Run Results keeps the completed-run identity and, when relevant, the delay baseline identity beside the baseline/compare actions; the adjacent message states the next valid action or why a control is disabled. Compare delays opens the Delay analysis window on its Against baseline tab, which states a zero positive additional delay and the number of unchanged occurrences explicitly.

`Import Legacy Case...` is an explicit conversion action. It reads a selected
external legacy directory into a new canonical scene; the source files are not
an editable or runtime fallback. After the source and destination are
validated, pending edits are resolved before conversion and the imported scene
opens without a second prompt.

## Main window layout

| Area | Contents | Enabled state |
|---|---|---|
| File menu | `New Case...`, `Open Case...`, `Recent Cases`, `Save`, `Save As...`, `Import Legacy Case...`, `Quit`, with separators between opening, saving, importing and quitting | New, Open, the legacy case picker, and Quit are always enabled. An open or New during a run waits until the run has stopped, and so does the opening of an imported legacy case; the import itself, which writes the destination scene, runs at once ([Close, New and Open during a run](../development/smoke-tests.md#close-new-and-open-during-a-run)). Recent Cases is enabled when the list has entries. Save and Save As are enabled when a scene is loaded. No command is in two menus of the menu bar. |
| Edit menu | `Undo`, `Redo`, `Find...` | Undo and Redo are enabled when a scene is loaded, no run is active and the history has a step to go back or forward; the text names the step. Find is always enabled. |
| Simulation menu and toolbar | `Run`, `Pause`, `Stop`, speed control; `Set Output Folder...` below them in the menu | Run is enabled when a scene is loaded, no run is active and no scene error diagnostics are current. Pause and Stop are enabled only while simulation is running. Set Output Folder is disabled while a run is active. |
| Central view | Existing network view and progress bar | Shows canonical scene infrastructure after open and simulation state after setup and run. |
| Editor docks | Case settings, infrastructure, rolling stock units, compositions, services and timetable, scenarios with their incidents and entrance delays, passengers | Enabled for a new or opened canonical scene. While a run is active the contents of the Infrastructure, Services, Scenarios and Passengers docks are disabled; their title bars stay enabled, so the docks can still be closed and floated. A legacy case must first be imported as a scene. |
| Validation panel | Dockable table of diagnostics | Available from View in every mode. Updated on open, edit, save, and pre-run validation; automatic summary detail is richer in Advanced mode. |
| Loaded Data panel | Case/source metadata, parsed category counts, scenarios, provenance, validation status, editor links, runtime and result readiness | Automatically raised after scene open only in Advanced mode; the existing View action remains explicit. Item activation reuses the existing network view, validation table, and domain editors. |
| Existing info dock (the inspector) | Read-only selected item details for nodes, stations, arcs, connections, signals, trains. Its entry in the View menu is `Details`. Before a run the title names the item: `Node <id>` (with `, platform <id>` for a platform node), `Station <id>`, `Arc <id>`, `Track <id>`, `Connection <id>` or `Signal <label>`. During a run it is `Node <id>`, `Station <name>`, `Arc <id>`, `Connection`, `Signal` or `Train <name>`. A field that only a run fills shows the placeholder `After a run` in the preview. | Enabled when the network view has selectable items. It stays read-only outside explicit editor controls. Before a run a click on an item also selects its row in the Infrastructure dock, and `Edit in Infrastructure` in the menu of the item also shows that dock (see [Editor panels](#editor-panels)). It closes when a run stops or its results are discarded. |
| Status bar | Transient messages, for example `Opened <name>`, `Running` and `Simulation complete`, which an edit replaces with `Run results discarded: the case changed.` (`Run results discarded and delay baseline cleared: the case changed.` when a delay baseline was set), and a delete in an editor with a message that names the item and says that Undo restores it, such as `Deleted scenario 'peak'. Undo restores it.`; three permanent fields: the follow status of the selected train, the validation summary and the zoom of the network view | Always visible. The scene name and the unsaved mark are in the window title. Normal mode calls out actionable validation errors; Advanced mode also shows warning/info counts. |

## Validation lifecycle

Validation runs when a bundle or directory opens, after each committed editor change, after Save or Save As, and immediately before Run. Each of these refreshes the validation panel, which validates the model again only when the model or the duration override of the next run has changed since its last validation. The model counts as changed after an edit, Undo or Redo, the reload of a linked unit file, any other write through `SceneDocument::mutableModel` (such as what Save records) and the opening or creating of a scene. Otherwise the panel shows the diagnostics of that validation again, because the validator reads nothing besides the model and its arguments. The check before Run is mandatory even when the panel already shows no errors: Run is refused while those diagnostics hold an error. Preparing the run then validates the model once more with the service occurrences that the run leaves out.

Structural diagnostics come from the bundle reader or directory loader and the required JSON files. If structural loading has errors, semantic validation is skipped to avoid duplicate noise from a partial model. Semantic diagnostics check topology, identifiers and references, rolling-stock values and traction intervals, timetable values, signalling coverage, incidents, base time, and repeated-service rules.

Save is allowed with semantic errors so students can preserve work in progress. Run is not allowed with any `SceneSeverity::Error`. Warnings and info diagnostics remain available in the full Validation table but do not block Run; normal-mode automatic presentation omits their non-blocking summary counts. Native builder diagnostics appear under **Run** in Loaded Data, and builder errors block Run.

The Advanced Details choice changes presentation only. It does not
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
simulation.makeNextRunOutputFolders()
teardownGUI
simulation.prepareScene(SceneModel)
setupGUI
```

An output folder that cannot be made stops Run before `teardownGUI`, so the
earlier run keeps its results, its replay and its canvas. A preparation that
fails after `teardownGUI` leaves no results, and the window draws the track
preview again. Both say "Run not started" with the reason in the status bar.
Only after setup succeeds does the simulation worker start. Opening or creating
a scene calls `teardownGUI` and `simulation.resetState` before it replaces the
current scene. Case replacement waits for the old worker to stop and ignores any
queued completion from that worker. Disabled scenario controls are not pending
edits.

The Loaded Data panel reports one global runtime state instead of claiming each
input file built a separate runtime model. It is `Not built`, shown as **Not run**, after open or an
edit, `Ready` after `prepareScene` succeeds, `Running` while the worker runs,
`Completed` when a run ends with results, `Stopped` when a stop request ends a
run before it completes, and `Failed` with builder diagnostics after preparation
fails. `Failed` also covers a run that ends without results, unless a stop
request ended it before it completed, and a run whose scene changed while it ran. Completed simulation output
is reported as available, but remains outside canonical input. The Case row counts the errors and warnings of the
run with those of the validation and the loaded data, except `run.output.*`: an output folder or file that the run
cannot write says nothing about the case.

## Editor panels

Each editor dock holds one panel from `editors/`, a [`SceneEditorPanel`]. A panel shows the model of the window's `SceneDocument` and changes it with `edit`, which takes the parts that it changes, a label and the change itself. While a run uses the scene `edit` refuses a change and leaves the model as it is, unless the change touches `OpenDuringRun`: the case settings, the rolling stock units and the compositions, which the guide keeps open during a run. The infrastructure facets change the model through `InfrastructurePanel::edit` and `editInPlace`, which call `edit` with the infrastructure, so a combo in their table that changes during a run changes nothing either. Writes that are not an edit of a panel go through `mutableModel` instead: the units that `TrainUnitSourceLinker` reloads from their files, what `SceneSession` records when it saves, and the default scenario that the window sets when it opens a scene without one. A panel also emits `changed`, which the window answers by running validation again. The window creates the docks, gives every panel the document, and connects the panels with each other and with the run. It holds no editing code of its own.

`SceneDocument` owns the `SceneModel`. It knows whether a scene is loaded, whether it differs from what was saved, whether a run lets it be edited, and how often it has changed. `revision` counts the edits, the Undo and Redo steps and the scenes opened or created; `modelGeneration` also counts every call of `mutableModel`, so a result computed from the model, such as the validation, is current while it stays the same. Its signal `aboutToEdit` comes before an edit changes the model, `changed` names the parts that changed, `replaced` follows the opening or creating of a scene, and `dirtyChanged` and `editableChanged` follow the two flags. The window answers `aboutToEdit` by keeping the run selection and the links of the rolling stock units with the live state of the history, and `changed` by adding an Undo step and by clearing the run results and the delay baseline. When no run goes on, a canvas that still shows a run, its last step or a step of its replay, gives way to the track preview (`invalidateRunResults`), so Follow and the run entries of the map key end with the results, as they do when another scenario is selected. A run that goes on or is paused keeps its canvas. A panel that shows data from another part of the scene, such as the targets of an incident, overrides `documentChanged` and refreshes itself for the kinds that it shows. Only `ScenarioPanel` also follows a kind that it edits itself: the incident and entrance delay panels edit the scenarios as well, and it answers a change of the scenarios by updating the labels of its list and its header, which leaves its fields as they are. `TrainUnitPanel` is refreshed through the `reloaded` signal of `TrainUnitSourceLinker` when a linked file changes the units. Undo and Redo put an earlier state back with `restore`, which emits no `changed`, so the window refreshes the panels itself.

Saving, the question about unsaved changes, validation and Undo first take the text of every field that is still being typed: the window calls `commitPendingEditors` on each panel in turn. This includes a cell of the infrastructure table whose editor is still open, which is taken as leaving the cell takes it, except while the infrastructure panel tells an edit of its own: its table then still shows the rows from before the edit. The reload of a linked unit file is the exception, because no user action starts it: it takes the text of the Rolling Stock Units panel, and its validation takes that of the Scenario and Passenger panels. Only its question takes the text of every field (see the rolling-stock links under V1 edit panes). A panel that refreshes for an edit of another panel would write the stored values over that text, so it takes its own text first with `takePendingText`, as an edit of its own that follows the other one in the history: the passenger and entrance delay panels for the case settings, the infrastructure and the services, the service panel for the infrastructure and the compositions, the composition panel for the rolling stock units, and the scenario and incident panels for the infrastructure. A panel does this only for parts that it never edits itself, because its own edit may be the one that is being reported. Its signals are blocked meanwhile: the other panel is still in the middle of its edit, and the window answers `changed` by taking the text of every panel, that one too. The window validates both edits when the other panel emits `changed`. A number field of the case settings, service, incident, entrance delay, passenger and train unit panels takes its text only while it has the focus, and a field that shows a stored number at a limit of its range does not write that limit back. So a number that the user did not edit stays as it is stored, without an Undo step. While a run uses the scene the document is not editable, and the service, scenario, incident and entrance delay panels take nothing.

The Infrastructure panel lists one kind of entity at a time in a table. Each kind is an [`InfrastructureFacet`] in `editors/infrastructure/` that knows its columns and rows and what an edited cell, an addition and a deletion do to the model. `TopologyFacets` holds tracks, nodes, arcs and connections, `BlockRouteFacets` blocks, routes, block dependencies and single-track restrictions, `StationFacets` stations, platforms and station boundaries, and `SignalFacets` signals and signalling areas. A facet decides first whether a cell, a button or a combo in its table changes anything, and only then changes the model through the panel, so a value that stays the same adds no undo step. Renaming an entity rewrites what refers to it (`scene/SceneReferences`), and an entity that others use cannot be deleted.

The facet combo is on top of the panel, and the buttons that add and delete an entity are in a row under the table. Between the table and the buttons a line (`markLabel`) says what the canvas marks of the selected row, such as "route2: 124 sections on B3, B0, B4, B5 and B2". It is hidden without a row and while a run uses the document, and the window hides it with `setMarkShown` while the canvas shows a run or its results, which mark no row. The canvas shows the mark only while the Infrastructure dock is in view: `EditorDocks` tells the window when the dock is shown or hidden and when its tab comes to the front of its group or goes behind another one (`QDockWidget::visibilityChanged`), and once after it hides the new dock, for which Qt sends no signal. The window then shows or hides the mark with `NetworkPresenter::setSelectionMarkShown` and lists the mark in the map key, as `Selected track` for a track and `Selected item` for a row of any other kind, only while the mark shows. A hidden mark keeps its row, and a row selected meanwhile, as a click on the canvas selects one, places it hidden without moving the view. So the dock brings back the same mark, without a new selection and without a change of zoom or position. A facet names the other controls it uses with `capabilities`, and the panel shows only those of the chosen facet and calls the facet's functions of a control only when it has its capability. Blocks have the filter on a track, and Add is off while the filter names no track of the scene. They also have the buttons that insert and move a block, next to Add and Delete. Those buttons keep their room while another kind hides them: they are the widest controls of any kind, so the panel needs the same width for every kind, and the dock and the canvas beside it keep their width when the kind changes. Under them a second row holds the task actions of the chosen kind (`taskActions`), such as Add Station... under Nodes. A task action works on the selected row, is off without one and while the scene cannot be edited, and changes the model in one edit, so in one Undo step. The row is hidden for a kind without task actions, and it is narrower than the first row, so the panel keeps its width. Routes have their sections, listed under the heading "Route sections" below the table, in a splitter that the user can drag. The sentence above the table comes from `infrastructureGuidanceText`, which has a sentence of its own for each of the thirteen kinds when its table is empty. It names a prerequisite (a track, two nodes, a station) only while the scene lacks it, says that the scene cannot be edited while a run is in progress when Add is off, and asks for a case when none is open.

Under the facet combo, and under the filter of blocks, a row holds the text filter (`filterEdit`) and the count of the rows (`countLabel`), such as "12 of 399 nodes", and for the facets with the capability `TrackRowFilter` (nodes, arcs, connections and platforms) a filter on a track (`trackFilterCombo`) at its start. Such a facet says with `rowOnTrack` whether a row lies on a track: a node or an arc on it, a connection with an end on it, a platform with a node on it. A row is shown when it lies on the chosen track and every word of the text occurs in one of its cells, ignoring case: `FindDialog::matches`, the rule of the Find dialog. The text of a combo or a number field in a cell counts as its text. The filters hide rows with `setRowHidden` and keep them in the table, so a row stays the entity at that index of the model, and an edit, Delete and the mark of the selected row find their entity. The panel applies the filters again whenever it fills the table, and they stay when the kind changes. A row that they hide is never selected: a filter that hides the selected row clears the selection, and so does an edit that moves the entity out of the filter. Add and Insert clear the text filter and the filter on a track first, so that the new row is shown, and keep the filter of blocks, which names the track of a new block. Move Up and Move Down are off while a filter hides a row, because a move swaps a block with its neighbour in the list, which the filter can hide. The table keeps room for two rows: in a low dock the dock scrolls the panel instead.

A facet offers the entities that a cell refers to with `choices`: the tracks for the track of a node, the nodes on the track of an arc, with their kilometres, for its ends, and every node, with its track, for the ends of a connection. For such a cell the delegate of the table opens an editable combo, which completes from any part of a label, ignoring case, and writes the id of the chosen label into the cell; a typed id, in any case, is that entity. Choosing in its list ends the edit as Return does, the keys up and down move to another row as from a text cell, and the combo is as wide as its label, over the cells to its right. The combo exists only while the cell is edited, so filling a table of thousands of rows creates no combo: a combo in every row would add more than a second to the nodes of Copenhagen and six seconds to its arcs. `ListFacet::refusal` refuses an id that the scene does not have with a message that names it, and an empty one with a message that asks for one, such as "Choose a node", and the nodes of a platform are not typed: the Nodes cell of a platform is read-only, and Choose Nodes... or a double click on the cell (`activateCell`, also Return on Windows and Linux) opens a checkable list with a filter field (`UserPrompts::chooseItems`). The list holds only nodes that the scene has, turns off the nodes that another platform or station has, and shows an id that the scene lacks as missing, so that it can be removed. One change of the list is one edit. A flag is a check box (`setFlagCell`): its cell has no text and takes none, and a click changes the flag in one edit. Nodes and platforms have a Station column, which is not edited, that names the station as the Details dock does with `previewStationMemberships`: the station of a platform that lists the node, or a station without platforms at the position of the node, by its name or else its id.

A click on an item of the preview, after the Details dock has shown it, and `Edit in Infrastructure` in the menu of the item select the row of its entity with `InfrastructurePanel::selectCanvasItem`. A node, a track, an arc, a connection and a station are rows of their own kind. A segment of a track line is its arc, or the track when no arc joins its two nodes. A station node is the row of its platform under Platforms, or its row under Nodes when it has no platform. A signal head names a block section, and is the row under Blocks of the block of that section, with the track filter set to the track of the block. The panel takes an open cell editor first, switches the facet combo and the filter of blocks without their signals, clears the text filter or the filter on a track when it hides the row, selects the row without opening an editor, scrolls to it, and emits `selectionChanged` once. The window answers it under a guard (`m_selectingRowFromCanvas`): the mark moves in place as for any other row, and the preview is not drawn again, so the view keeps its zoom and position and the Details dock its item. When the model has changed since the preview was drawn, as an edit in another dock changes it, only a new preview marks the row: the window draws it with `drawKeepingView`, which gives the view back its zoom and centre to a fraction of a pixel, as Undo does, and shows the item in the Details dock again. `Edit in Infrastructure` also shows and raises the dock, which shows the mark, and for an item without a row it says in the status bar that the item has no row in Infrastructure. With a run or its results on the canvas, while the contents of the dock are disabled, and for an item whose entity the model does not have, a click only shows the item in the Details dock.

The Validation and Loaded Data docks hold the table and the tree of a [`ValidationPanel`], which is not an editor. Its `refresh` validates the scene of the document when its `modelGeneration` or the duration of the next run has changed since the panel last validated it, and otherwise takes the diagnostics of that validation again. It writes the diagnostics to the list that the window and the scenario and passenger panels read, fills the table, the validation summary of the status bar and the tree, and emits `refreshed`, which the window answers by refreshing the scenario and passenger panels and the actions. The window gives the panel the duration of the next run, the Advanced details choice, and what the tree shows of the scene file and the run. A row of the tree that stands for a scene item emits `targetActivated`, and the window opens the editor or the view. A row of the table whose diagnostic names an item emits `itemActivated` when it is activated, and the window selects the row of an infrastructure item (a track, node, arc, block, connection, station, signal, signalling area or route) in Infrastructure, or opens the editor of a unit, composition, service or incident; a row without an item does nothing.

Each panel has a widget test, and each group of facets one. The editor smoke and the creator acceptance smoke run the panels in the whole window ([Editor Smoke Test](../development/smoke-tests.md#editor-smoke-test)).

## V1 edit panes

Editing uses explicit fields, table cells, and add, duplicate, move, and delete
controls. The network view previews canonical infrastructure; it is not a CAD
surface.

| Pane | Editable in V1 | Derived or read-only |
|---|---|---|
| Case settings | Name, description, base time, duration, buffer, recovery | Schema version, units, scene path, validation summary |
| Infrastructure | Tracks, nodes, arcs, blocks, connections, stations, platforms and platform geometry, signals, signalling areas, routes, dependencies, single-track restrictions, station boundaries | Network geometry preview and runtime diagnostics |
| Rolling Stock Units | ID; copies of the units of the bundled cases with Add from Library, from the resource `rolling_stock_library.json`; Traction tab with the engine section (traction source, rows and plot); Physical tab with the characteristics section (nine native physical fields and parameter source); session-only Link/Unlink/Retry controls for physical and traction files | Static traction plot; composition usage; live link status |
| Compositions | ID and ordered rolling-stock-unit membership | Selected-unit source references and traction plot |
| Services and timetable | ID, operating code, composition, route, entry time, performance, optional speed cap, repeat count/headway/code step, run selection, ordered stop matrix and stop dialog | Generated occurrence identities and offsets |
| Scenarios | Scenario metadata; signal failures, train breakdowns, and entrance delays; targets, windows, occurrence, reduced speed, recovery, destination termination | Target choices derived from signals, blocks, routes, services, and timetable stops |
| Passengers | Passenger IDs; journeys, absolute time windows, and station endpoints; ordered service-occurrence legs; append import from the exact DAS and RouteChoice file pair | Row-specific import outcomes and validation diagnostics; errors of the selection in one callout and warnings and notes in a second |

The signalling area table of the Infrastructure pane has a list for the
signalling system and a list for the track. The system list holds the six levels
with the names of [Signalling levels](../guides/signalling-levels.md), which also says what
each level does. The track list holds "(all tracks)" and each track of the scene
with the extent of its blocks. A value that does not exist is listed first as
"Invalid level N" or "X (missing)". An area whose system is not chosen lists
"Choose a signalling system" first. A new area starts with the extent of the
network and no system, and the case cannot run until a system is chosen.

Rolling-stock links are UI session state, not scene schema. The watcher keeps an
absolute selected file and its parent directory armed through atomic replacement,
deletion, and recreation; a short debounce groups callbacks for one file. A
complete validated candidate is compared with the last accepted values before
the canonical unit changes. Shared links use one grouped Keep Edits/Reload
decision; a focused local edit prompts for a conflict decision, while an update
observed during a run is deferred and offered after completion. A change is
taken without a user action, so the linker takes only the text that it needs.
Once the file holds something new, it takes the text of the Rolling Stock Units
panel before it compares the units with the file. Only before the Keep
Edits/Reload question does it take the text of every panel: the question takes
the focus, and a field that takes its text when it loses the focus would change
the scene while the question is open, so the answer would be stale and the
question would come back. The validation after a reload takes the text of the
Scenario and Passenger panels, which it shows again from the scene, and leaves
every other field as it is, also an open cell of the infrastructure table.
New scenes, successful opens, unlink, deletion, and relinking clear
the corresponding observers, and Undo or Redo that brings a unit back restores
its links and observers; failed or cancelled opens leave the current links
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
changed. The Colour control follows it: a swatch, Choose...
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
Before the panel copies the service, it emits `commitPendingEditsRequested`, and
the window takes the text that is still being typed in the other panels and
validates the scene when that text changed it. On
macOS a button does not take the focus, so a composition id typed without Enter
would otherwise be taken when the dialog takes the focus, and its change of the
service would keep the stop of the dialog out. Accept validates the focused fields and commits once; Cancel changes nothing,
including when adding a stop. An accepted dialog changes the service only while
the scene can be edited and the first service with its id still equals the copy
that the dialog started from, so a scene that replaced the open one with an equal
service takes the stop. An added stop enters only when `insertSceneStop` takes it
on the live model. Otherwise the dialog changes nothing and shows no message. The matrix and dialog offer Elapsed/Clock time
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

## Direction of infrastructure editing

The Infrastructure tables stay the way to edit infrastructure. Issue #1180 makes them easier to use in three steps, in this order, each its own change:

1. A click on an item of the network view selects its row in the Infrastructure dock (#1218, see [Editor panels](#editor-panels)).
2. The tables get a text filter and a row count, a track filter on nodes, arcs, connections and platforms, station names next to node and platform ids, combos instead of typed ids for references to tracks and nodes, and check boxes for flags (#1219, see [Editor panels](#editor-panels)).
3. Task actions add the rows of a common change together as one undo step, starting with `Add station at node` (#1220), whose rules are written down under [Add station at node](#add-station-at-node).

The tables stay for everything the actions do not cover. A drawing editor is not planned.

### Add station at node

`Add Station...` is a task action of the Nodes table. It works on the selected node, which a click on the node in the network view also selects. It asks for the name of the station and then adds, in one edit and so in one Undo step:

- a station whose id and name are the name, without a position;
- one platform of that station, with the id `<name>.platform.1`, the node as its only node, and no length or width, so that the defaults of the model, 100 m and 2.5 m, apply as they do to a platform that Add creates under Platforms.

These are all the rows that a valid station needs. A station needs a position or a platform (`scene.station.anchor.missing`), and a platform needs a node (`scene.platform.nodes.none`) that the case has (`scene.ref.unresolved`) and that no other platform has (`scene.platform.node.conflict`). Nothing else has to refer to a station: a station view, timetable stops, passenger journeys and station boundaries are optional, so the action adds none of them.

The station gets no position. For a station with platforms a position only sets the X of the station in a run, which would then stay behind when the node moves; without one the run takes the X and Y of the station from the first node of its first platform. It gets no station view either, because the latitude and longitude of a node are not known, and the network view draws the station at its platform node.

The action changes nothing, and says why in the status bar, when:

- the node already belongs to a station, as the Station column shows it (`previewStationMemberships`): a platform lists it, or a station without platforms has its position at the X of the node;
- the case already has 95 stations, the most that a run takes (`RuntimeLimits::kNativeMaxStations`, `scene.capacity.runtime`);
- the name is empty once the spaces around it are removed, or holds a tab or a line break, which the legacy export and the key of a platform row cannot hold;
- a station already has the name as its id or as its name. The message points to Platforms, where a platform is added to an existing station.

It checks the node and the number of stations before it asks for the name. Cancel changes nothing. Afterwards the node stays selected and the status bar says that Undo removes the station. The new rows raise none of `scene.id.empty`, `scene.id.duplicate`, `scene.station.anchor.missing`, `scene.station.position.invalid`, `scene.platform.nodes.none`, `scene.ref.unresolved`, `scene.platform.node.conflict`, `scene.platform.length.invalid`, `scene.platform.width.invalid`, `scene.platform.capacity.invalid`, `scene.capacity.runtime`, `scene.native.id.duplicate` and `scene.native.binding.missing`, and a case that had no station or no platform loses `scene.stations.none` and `scene.platforms.none`.

## Explicit out-of-scope list

- Drag-and-drop railway CAD, route painting, or automatic signal placement.
- Automatic timetable optimization or performance sweeps.
- A generic scenario scripting language.
- Treating legacy files as the editable source of truth.
- Editing legacy source files from the UI.
- Treating an explicit interoperability export as a scene directory.
- General-purpose JSON text editing inside the application.

[`SceneModel`]: https://egtrain.samuelbruin.com/latest/api/struct_scene_model.html
[`SceneDiagnostic`]: https://egtrain.samuelbruin.com/latest/api/struct_scene_diagnostic.html
[`SceneDocument`]: https://egtrain.samuelbruin.com/latest/api/class_scene_document.html
[`DispatchController::prepareScene`]: https://egtrain.samuelbruin.com/latest/api/class_dispatch_controller.html
[`SceneEditorPanel`]: https://egtrain.samuelbruin.com/latest/api/class_scene_editor_panel.html
[`InfrastructureFacet`]: https://egtrain.samuelbruin.com/latest/api/class_infrastructure_facet.html
[`ValidationPanel`]: https://egtrain.samuelbruin.com/latest/api/class_validation_panel.html
