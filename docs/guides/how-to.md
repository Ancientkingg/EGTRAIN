# How-to

Short recipes for the common tasks. Each says what you need, what to do and what you should see. [Getting started](getting-started.md) is the walk from a download to a first run, the [glossary](glossary.md) defines the words, and [Common questions](common-questions.md) answers short questions. On macOS the key Ctrl in a shortcut is the Command key, and [Keyboard shortcuts](keyboard-shortcuts.md) lists every shortcut.

- Open and save
  - [Open a case](#open-a-case)
  - [Copy and save a case](#copy-and-save-a-case)
  - [Answer the unsaved changes question](#answer-the-unsaved-changes-question)
  - [Convert a legacy case and write legacy files](#convert-a-legacy-case-and-write-legacy-files)
- Run and watch
  - [Run a case](#run-a-case)
  - [Fix validation errors](#fix-validation-errors)
  - [Move around the network view and follow a train](#move-around-the-network-view-and-follow-a-train)
  - [Replay a completed run](#replay-a-completed-run)
  - [Close, start a new case or open another during a run](#close-start-a-new-case-or-open-another-during-a-run)
- Change a case
  - [Edit a timetable stop](#edit-a-timetable-stop)
  - [Add a train](#add-a-train)
  - [Add a service](#add-a-service)
  - [Edit rolling stock](#edit-rolling-stock)
  - [Change the infrastructure of a case](#change-the-infrastructure-of-a-case)
  - [Add an incident or an entrance delay](#add-an-incident-or-an-entrance-delay)
- Results
  - [Results and exports](#results-and-exports)
  - [View a graph of a run](#view-a-graph-of-a-run)
  - [Compare a scenario with a baseline](#compare-a-scenario-with-a-baseline)
- Settings and problems
  - [Change the application settings](#change-the-application-settings)
  - [If EGTRAIN closes unexpectedly](#if-egtrain-closes-unexpectedly)
  - [Why a command is disabled](#why-a-command-is-disabled)

## Open a case

You need a case: a bundled case, an `.egscene` file or a case folder.

Use one of these:

- Click **Open Case** in the toolbar, or choose **File > Open Case...** (Ctrl+O). In **Open a Case**, select an entry under **Bundled cases** or **Recent cases** and click **Open**. A long case name is shortened in the middle of the row; hold the pointer over the row to see the whole name and the path. **Other ways to open...** offers **New Case...**, **Open Case File...**, **Open Case Folder...** and **Import Legacy Case...**.
- In **Open a Case**, choose **Other ways to open... > Open Case File...** and select an `.egscene` file.
- In **Open a Case**, choose **Other ways to open... > Open Case Folder...** and select a folder that holds `scene.json`.
- Choose **File > Recent Cases**. It lists the last 8 cases you opened or saved, each under the name that **Open a Case** shows (a saved `.egscene` file without its extension). The bundled cases are not listed there; **Open a Case** has them under **Bundled cases**. The menu checks the files each time it opens. An entry whose file is gone reads `Unavailable:` and is disabled, and the menu is greyed out while the list is empty.
- Drag one `.egscene` file onto the window. A folder or several files are refused with `Cannot open the dropped file: <reason>` in the status bar.
- On Windows and macOS, double-click an `.egscene` file ([Open by double-click](opening-a-case-study.md#open-by-double-click)).
- From a terminal, `QEGTRAIN --scene path/to/case.egscene` opens a bundle or a case folder, and `QEGTRAIN path/to/case.egscene` opens a bundle ([command-line options](command-line.md)).

The dialogs of **Open Case File...** and **Open Case Folder...** start in the folder above the open case. With no case open, they start in the `Scenes` folder of the app on macOS, and in your home folder on other systems.

You should see the window title `EGTRAIN - <case name>`, the network in the view and the status bar `Opened <case name>`. Opening a case does not run it and does not change the file.

If the case does not open, a dialog says why: **Cannot Open Case**, **Older Case Not Supported** or **Newer Case**. [Opening an `.egscene` case](opening-a-case-study.md) explains each. While the current case has unsaved edits, see [the unsaved changes question](#answer-the-unsaved-changes-question).

Normal mode keeps the network view and the editors uncluttered. **View > Advanced Details** switches to advanced mode, which raises the dock **Loaded Data** automatically after a case opens and shows the full validation counts, the non-blocking warnings and the technical inventories. In normal mode **View > Loaded Data** and **View > Case Validation** still open those docks. The choice changes what is shown, not what a run does.

## Copy and save a case

A bundled case is read-only in practice: EGTRAIN never saves into the installation. Make your own copy before you edit.

1. Choose **File > Save As...** and click **Case File...**. The question also offers **Case Folder...**, and its default button is the format of the open case.
2. Choose a folder of your own and a file name. EGTRAIN adds `.egscene` when you leave it off, and asks before that name replaces a file. The dialog starts in the folder of your open case, or in your Documents folder for a bundled case.
3. Save.

You should see `Case saved` in the status bar. Your file is now the open case, and it appears in **File > Recent Cases**. The original is unchanged. The file holds the inputs of the case and no results, replay or delay baseline.

Other ways to save:

- **File > Save** (Ctrl+S) writes the case back to the bundle or case folder it came from. For a bundled case, and for a new case that has no file yet, it asks for a new `.egscene` file instead, as **Case File...** of **Save As...** does, and Cancel keeps the edit. A case folder outside the installation, such as one in a source checkout, is saved in place. If the files of the case changed on disk since you opened or last saved it, for example by a hand edit or another EGTRAIN window, the question **Case Changed on Disk** asks whether to replace the case on disk with your version. No cancels the save and keeps your edits; **Save As...** then keeps both versions.
- **File > Save As...** with **Case Folder...** writes the case as a folder of JSON files, which you can edit by hand. If the folder you choose is not empty and holds no `scene.json`, EGTRAIN asks `The folder is not empty. Save the case there anyway?`. If it holds another case, EGTRAIN asks whether to replace that case. Either way other files in the folder are kept, and the folder of the open case is saved again without a question.
- A destination inside the installation is refused with **Cannot Save Case**: `Bundled cases cannot be overwritten. Choose a location outside the EGTRAIN installation.`

To check a saved case, open it again from **File > Recent Cases**. **Save** and **Save As...** are enabled whenever a case is open, also during a run.

## Answer the unsaved changes question

After an edit the case is unsaved. EGTRAIN then asks (`Save the changes to <case name>?`) before it replaces the case, which happens when you open another case, choose **New Case...**, load a legacy case or close the window. On Windows and Linux the box is titled **Unsaved Case** and offers **Save**, **Discard** and **Cancel**. On macOS it is a sheet without a title that offers **Save**, **Cancel** and **Don't Save**, which does what **Discard** does.

- **Save** saves the case first. For a bundled case or a new case it asks for a new `.egscene` file, and if you cancel that dialog, the open or close is cancelled too. The same holds when you answer No to **Case Changed on Disk**.
- **Discard** (**Don't Save** on macOS) drops the edits.
- **Cancel** keeps the case and its edits.

If EGTRAIN closes without an answer, for example after a crash, it offers the edits again at the next start in **Recover Unsaved Work** (see [Getting started](getting-started.md#if-something-goes-wrong)). **Edit > Undo** and **Edit > Redo** step through the edits of the session.

## Convert a legacy case and write legacy files

A legacy case is the input folder of earlier versions. EGTRAIN converts it into a new case folder and leaves the source unchanged.

1. Choose **File > Import Legacy Case...**, or **Other ways to open... > Import Legacy Case...** in **Open a Case**. Both go straight to the folder dialogs.
2. Select the legacy case folder in **Select Legacy Case Folder**, then a destination in **Select Case Destination**. The destination must already exist; a name that does not exist gives `Directory not found`. An empty folder takes the case (the folder dialog has a button for a new folder). A folder that holds other files is left alone: EGTRAIN offers to import into a new folder inside it, named after the legacy folder, and if a folder of that name already holds files, **Cannot Import Legacy Case** says so and nothing is written. A folder that holds another case (a `scene.json`) is replaced, with everything in it, only if you answer Yes. The destination must be separate from the source and outside it, or **Cannot Import Legacy Case** says so. If the open case has unsaved edits, EGTRAIN asks about them after you have chosen both folders and before it imports.
3. If the import reports errors, warnings or notes, the box **Legacy Case Imported** counts them, for example `Import: 1 error, 2 warnings`, and lists the first findings. An import without findings shows no box. The converted case then opens. `scene_tool export` writes the base time and the simulation settings to `EGTRAINInitialParameters.json`, and the import reads that file. A legacy case without it whose name is not a known case has no base time and no duration: the case reports two validation errors and cannot run until you set both in **Editors > Case Settings**. The details are in `import_report` in `scene.json`, and the dock **Loaded Data** shows the counts.

From a terminal:

```bash
scene_tool import path/to/legacy-case path/to/new-case CaseName
scene_tool validate path/to/new-case
```

The window cannot write legacy files. `scene_tool export path/to/case path/to/legacy-output` writes them for another tool, and edits to those files do not flow back into the case. [Convert a legacy case](scene-schema.md#convert-a-legacy-case) lists what the conversion preserves, and the [migration matrix](v1-scene-migration-matrix.md) says what each legacy file becomes.

## Run a case

You need an open case with no validation error. [Getting started](getting-started.md#4-run-the-simulation) walks through a first run.

1. Click **Run** in the toolbar, or choose **Simulation > Run** (Ctrl+R).
2. Read the dialog **Run simulation** and click **Run simulation**. **Cancel** returns to the window without a run.

The trains move on the network view, and the status bar reads `Running`. **Pause** and **Stop** are in the toolbar.

A run starts its clock at the **Base time** in **Editors > Case Settings**.

## Fix validation errors

You need an open case. EGTRAIN checks it when it opens, after every edit and before every run. An error disables **Run** and a warning does not.

1. Look in the dock **Case and Layers**. It names the case, and when an error blocks the run, the line under the name shows the first error.
2. Choose **View > Case Validation** (Ctrl+Shift+V). The table lists the findings, errors first, with **Severity**, **Code**, **Message**, **File**, **Path** and **Fix**. Without **View > Advanced Details**, **Code**, **File** and **Path** are in the tooltip of a row instead.

   ![The dock Case Validation of Paimpol practice, a copy of Paimpol, after a dwell edit, with the columns Severity, Message and Fix. Row 1 is the warning Minimum dwell is longer than the time from arrival to departure; the rows below it read Journey has no route-choice legs.](../images/how-to-scene-validation.png)
3. Fix the first error in the editor that matches its **File** and **Path** (menu **Editors**), and read the table again. It updates after each edit.
4. Repeat until no error is left. **Run** is then enabled.

You can save a case that has errors, also one with a new service, incident or row whose references you have not chosen yet; the table names that entry, such as `Service new_service has no composition`. Warnings do not stop a run, but read them: the dialog **Run simulation** repeats the one about route sections that have no signalling level. Examples of errors and their usual fixes:

- A service has no composition or route, or names one that does not exist. Choose an existing one in **Services**, or add the missing record.
- A stop names a platform that the route does not reach, or the stops are out of route order. Correct the stop on the tab **Timetable** of **Services**.
- A rolling stock unit has no traction rows, or its rows overlap, are out of order or have a lower speed that is not below the upper speed. Correct them in **Rolling Stock Units**.

**Run** stays disabled while any error remains. From a terminal, `scene_tool validate --runnable path/to/case` prints the same findings ([scene_tool](command-line.md#scene_tool)).

## Move around the network view and follow a train

- **Zoom.** Turn the mouse wheel to zoom around the pointer, or use the zoom buttons of the toolbar (Ctrl++ and Ctrl+-). On a trackpad, a pinch zooms. **Fit** (Ctrl+0) shows the whole network.
- **Pan.** Drag the view, also at Fit. On a trackpad, two fingers move it. The view stops when the edge of the network reaches the middle of the view.
- **Select.** Click a track, connection, node, station, signal, train or passenger. The information panel for it opens, or for a passenger a box of its details above it, and a light blue ring marks the item. Click an empty part of the view, or close the panel, to remove the ring.
- **Find.** Choose **Edit > Find...** (Ctrl+F) and type part of the name or ID of a station, signal or train. A signal is listed by its track, its place and its direction, such as `Signal: track B0 at 0.23 km, facing right`; during a run the list adds the ids of the signals of the case that protect its section, such as `Signal S1: track B0 at 0.23 km, facing right`. The label or the id of one of its sections, such as `@1-B0@`, finds it too. Up and Down choose one in the list and Enter goes to it: the view centres on the item, zooms in so that it can be read, and selects it as a click does, so a running train is also followed. Esc closes the box. Trains are listed while a run or a replay shows them; signals are not listed while the Signals layer is off.
- **Context menu.** Right-click an item. A train offers **Show details**, **Add train breakdown**, **Center in view**, **Follow train** and **Copy train index**. Most other items offer **Show details**, **Center in view** and a copy command, and a signal head also offers **Add signal failure** after **Show details** ([add an incident](#add-an-incident-or-an-entrance-delay)). During a run a signal head offers **Copy signal location** and **Copy section ID**, which copies the id of the section that **Protected section** shows in its information panel. Right-click the empty view for **Fit whole network**, **Clear selection** and **Stop following train**.
- **Layers.** The check boxes in **Case and Layers** hide stations, station names, trains, speed labels, signals and passengers. **Passengers** is the same switch as **View > Passengers**. It is off at the first start and keeps the last choice; turning it on takes effect from the next run.
- **Follow.** Choose a train in the list next to **Follow** in the toolbar and click **Follow**. The view moves with the train, also while the run is paused. At Fit the view stays where it is; zoom in to follow the train. Click **Follow** again, or choose **Stop following train**, to stop.

**Follow** is disabled before a run and in a case without services, and its tooltip says why. A train that has not entered the network is listed with its scheduled entry time, for example `Rail-1 (scheduled 08:25:50)`, and Follow starts when it enters. The label at the right of the status bar names the state of the selected train, and its tooltip says whether the train is followed and why not. In a live run Follow switches off when the train leaves the network. In the replay of a completed run it stays on after the train has left, and it continues when you go back in time. [Scenario library and student loop](scenarios.md) describes the camera in detail. For the result windows, see [Inspecting diagrams](diagram-navigation.md).

## Replay a completed run

You need a run that reached its end. A stopped or failed run has no replay, and a stopped run keeps no results either.

1. When the run has ended, the **Replay** row appears under the network. Click **Start**, **Play** or **End**, the icon buttons left of the slider, or move the slider. The arrow keys and the mouse wheel move the slider too.
2. Each selection shows the last recorded frame at or before the chosen time, not an interpolated position. The label beside the slider shows the clock time of the frame in the format of the progress bar, for example `08:20:30`. **Play** advances one simulated second for each second of real time, and the button then turns into **Pause**.
3. When the replay holds the whole run, the row has no further text. A frame is recorded every 5 simulated seconds.

   ![The Replay row under the network after a run of Paimpol, with Start, Play, End and the slider at 08:09:00, Follow on Guin-Paim-ALL-STOPS-1-1, and the context menu of that train open: Show details, Add train breakdown, Center in view, Follow train and Copy train index.](../images/how-to-replay-menu.png)

The replay has 128 MiB of memory. A run that is larger loses its oldest frames. A line under the controls then says from which time the replay starts, for example `Replay starts at 10:00:20. Earlier times did not fit in memory.` The slider covers only the kept part, and **Start** goes to its first time. The tooltip of **Start** says so. If not even one moment fits, the row says `No replay` and `This run is too large to replay.`, and the slider and **Play** are disabled.

Train, station and signal selections stay across seeks. A train that has no position at the selected time is not drawn. Passenger journey details are not available in a replay, but the counts and statuses are. A new run, an edit of the case or another scenario clears the replay and returns the view to the preview. An edit or another scenario also closes the info dock of the selection.

## Close, start a new case or open another during a run

Closing the window, **New Case...**, the open commands and **Import Legacy Case...** first ask for the unsaved changes question if the case has edits. They then ask the running simulation to stop. The status bar shows `Stopping simulation...` and the window stays usable. The command goes on when the run has stopped, and the current case is replaced only then, so an open that fails leaves it in place. A stopped run leaves no results.

## Edit a timetable stop

You need an open case that you have copied ([Copy and save a case](#copy-and-save-a-case)) and no active run. The contents of the dock **Services** are disabled during a run; the menu entry still opens it.

1. Choose **Editors > Services** and select the service in the list at the top of the dock. The dock has the tabs **Service**, **Timetable** and **Run occurrences**. Open the tab **Timetable**.
2. In the table of stops, click the row of the stop. The dialog **Edit Timetable Stop** opens with the station, the platform, **Minimum dwell (s)**, **Planned arrival** and **Planned departure**.
3. Change the field. **Planned time display** switches the dialog between **Elapsed offsets (s)** (seconds from the base time of the case) and **Clock time**. Blank means the time is absent, and `0` is the start of the simulation.
4. Click **OK** to apply the fields together, or **Cancel** to leave the stop as it was.

The buttons **Add Stop**, **Insert After Selected**, **Remove Stop**, **Move Up** and **Move Down** change the list of stops. After an edit the validation runs again, and the results, the replay and the delay baseline are discarded. Run again to see the effect. **Edit > Undo** reverts the edit. The rules for the times, the platforms and the insert positions are in [Edit and validate](editing-a-case.md).

For the whole exercise of changing a copy, saving it, opening it again, validating it and running it while the original stays as it was, follow the [tutorial](tutorial-modify-a-case.md).

## Add a train

A train is one run of a service, an [occurrence](glossary.md#occurrence). You need an open case that you have copied ([Copy and save a case](#copy-and-save-a-case)) and no active run. Repeat a service to run it more than once, or duplicate it to give the new train its own times.

To repeat a service:

1. Choose **Editors > Services**, select the service in the list at the top of the dock and open the tab **Service**.
2. Tick **Repeat every (s)**, type the time from one run to the next and press Enter.
3. Tick **Repeat count**, type the number of runs and press Enter. Without **Repeat count**, the number of runs is the **Duration (s)** of **Editors > Case Settings** divided by the time between the runs, rounded up.
4. Open the tab **Run occurrences**. It has a row for each run, ticked, with the run number added to the operating code and the entry time under **Entry**.

   ![The dock Services of Paimpol on the tab Run occurrences, reading 3 of 3 selected, with three ticked rows: Guin-Paim-ALL-STOPS-1-1 on route2 at +1550 s, Guin-Paim-EXPRESS-1-1 on route6 at +10 s and Guin-Paim-EXPRESS-1-2 on route6 at +3610 s, each with performance 100.](../images/how-to-add-a-train.png)

The picture shows Paimpol after the express `Guin-Paim-EXPRESS-1` got **Repeat every (s)** 3600 and **Repeat count** 2. Its second run, `Guin-Paim-EXPRESS-1-2`, enters 3600 s after the first, at `+3610 s`. In a run without a window of this change, it reached Paimpol exactly 3600 s after the first express. When the operating code is a number, **Code step** numbers the runs instead, for example 1723, 1725 and 1727 with a step of 2.

To add a train with other times, select the service and click **Duplicate**. The copy, named after the service with `_copy` at the end, comes after it in the list and is selected. It keeps every field of the service, the **Operating code** too, so give it a code of its own to tell its trains apart in **Run occurrences**. Then change its **Entry time (s)** and, on the tab **Timetable**, the planned times of its stops ([Edit a timetable stop](#edit-a-timetable-stop)).

A new occurrence is ticked on the tab **Run occurrences**, so the next run includes it. Run the case to see the new train.

## Add a service

You need an open case that you have copied, with a composition and a route for the new service, and no active run. [Edit rolling stock](#edit-rolling-stock) adds a composition, and the routes are in **Editors > Infrastructure** under **Routes**.

1. Choose **Editors > Services** and click **Add Service**. The service `new_service` is added at the end of the list and selected. Until it has a composition and a route, **Case Validation** names what is missing, such as `Service new_service has no composition`.
2. On the tab **Service**, choose the **Composition** and the **Route**. The list **Route** names each route by its first and last station and its ID, such as `Guingamp → Paimpol [route6]`. Without **Entry time (s)**, the train enters at the planned departure of its first stop.
3. Open the tab **Timetable** and click **Add Stop** for each stop, in the order of the route. In the dialog **Add Timetable Stop**, choose the **Station** and the **Compatible platform**, tick **Planned departure** or **Planned arrival**, type the time in seconds after the base time of the case and click **OK**. Give the first stop a departure and the last stop an arrival.
4. Choose **View > Case Validation** and fix what it lists ([Fix validation errors](#fix-validation-errors)). Then run the case.

   ![The dock Services of Paimpol on the tab Service for new_service, the last of three services: Composition Draisy, Route Guingamp → Paimpol [route6], Entry time (s), Repeat every (s) and Repeat count unticked, and Performance (%) 100.](../images/how-to-add-a-service.png)

The picture shows the tab **Service** of `new_service` on Paimpol, with the composition `Draisy` and the route `route6`. Its stops are Guingamp, with a departure at 5000 s from `Guingamp.platform.1`, and Paimpol, with an arrival at 7599 s on `Paimpol.platform.1`. A run without a window of this case completed with the train `new_service-1`, which reached Paimpol at 7528 s. The fields of a service are explained in [Editing and validating a case](editing-a-case.md#services).

## Edit rolling stock

You need an open case that you have copied and no active run. The editors **Rolling Stock Units** and **Compositions** are not disabled during a run, but an edit there discards the results of that run.

1. Choose **Editors > Rolling Stock Units**. **Add Unit**, **Add from Library...**, **Duplicate** and **Delete** change the list. Select a unit to edit its physical values and its traction rows (**Add Row**, **Delete Row**). **Plot Traction Curve** plots the curve. The number fields stop at wide limits, and a number that no rail vehicle has, for example one read from a file, gives the warning `scene.train.value.implausible` in **Case Validation**. The limits are in [Rolling stock](scene-schema.md#rolling-stock).
2. Choose **Editors > Compositions** to put units in order for a train: **Add Composition**, **Add Unit**, **Remove Unit**, **Move Up**, **Move Down**.
3. Choose the composition of a service in **Editors > Services**.

**Add from Library...** lists the rolling stock units of the bundled cases under the case that holds them, with their maximum speed, length and number of traction rows. **Add** copies the selected unit into the open case with its physical values, its traction curve and its source references. While **Also add a composition with only this unit** is ticked, as it is at first, a composition of the unit alone comes with it, under the ID of the unit. One **Edit > Undo** takes back both. When the case already has a unit with that ID and the same values, nothing is copied: that unit is selected and the status bar says so. A unit with that ID and other values stays as it is, and the copy gets the next free ID, such as `SLT06_2`; the message **Rolling Stock ID Adjusted** says `SLT06 was taken, so it is now SLT06_2`. A composition with that ID and other units also stays, and the new composition gets the next free ID. Where the units of each case come from is in `DATA.md` at the top of the repository.

**Link File...** on the tabs **Physical** and **Traction** of the editor of a unit reads the values of a unit from a parameter file or a traction file. The links last for the session: **Unlink** removes one, and reopening or replacing the case clears them. [Review what loaded](main-window.md#review-what-loaded) gives the file formats and the rules for a changed file.

## Change the infrastructure of a case

You need an open case that you have copied, because a bundled case is not saved in place ([Copy and save a case](#copy-and-save-a-case)), and no active run: the contents of the dock **Infrastructure** are disabled during a run. Choose **Editors > Infrastructure**. The list above the table chooses what the table shows, such as **Arcs** or **Platforms**. [Editing and validating a case](editing-a-case.md#infrastructure) describes the tables.

To change the speed limit of an arc:

1. Choose **Arcs** in the list above the table.
2. Type the ID of the arc in the filter field beside the track list, such as `B0.arc.226` on Paimpol. The table then shows that arc only, and the count beside the field reads `1 of 393 arcs`.
3. Click the cell of the arc in the column **Speed limit (m/s)**, type the new limit and press Enter. The table may need a scroll to the right to show that column.

The line under the table names the selected arc and where it lies, and the network view marks the arc with a violet band.

![The main window with the dock Infrastructure on Arcs, filtered to B0.arc.226 (1 of 393 arcs): track B0 from B0.node.127 to B0.node.128, curvature radius 10000, gradient -0.012 and speed limit 8.3333333330000006. The line under the table reads B0.arc.226: arc on B0 from 12.755 to 13.140 km, and the network view at 20x shows the arc as a violet band on the track.](../images/how-to-change-infrastructure.png)

The picture shows `B0.arc.226` of Paimpol, which runs from 12.755 to 13.140 km, with its limit lowered from 19.444 m/s (70 km/h) to 8.333333333 m/s (30 km/h), the limit of the short arcs next to it. The table writes a number with all the digits it holds, so the cell reads `8.3333333330000006`. In a run without a window of Paimpol with this one change, the express reached Paimpol 24 s later than before, at 2700 s instead of 2676 s.

To add a platform to a station:

1. Choose **Platforms** and click **Add**. The new platform, `platform`, goes to the first station of the case and has no node yet, so **Case Validation** reports `Platform has no bound nodes`.
2. In its row, set **Station ID** to the station, such as `Gourland` on Paimpol. The platform moves to that station.
3. Set **Nodes** to the node of the platform, or to several nodes separated by commas, such as `B0.node.42`, the node at 2.581 km next to `B0.node.41` of `Gourland.platform.1`. A node that the case does not have is refused. A node can belong to one platform only; a second one gives the error `Node is bound to more than one station/platform`.
4. Read **Case Validation**. A service that stops at the station without a platform now gives the error `Stop has no platform and the route reaches more than one here`, and its **Fix** names one of the platforms. On Paimpol this is the stop of `Guin-Paim-ALL-STOPS-1` at Gourland. Open that stop on the tab **Timetable** of **Services** ([Edit a timetable stop](#edit-a-timetable-stop)), choose a **Compatible platform** and click **OK**.

With a platform for that stop, the case validates and runs again. Save the copy to keep the change.

## Add an incident or an entrance delay

Incidents, which are signal failures and train breakdowns, and entrance delays belong to a scenario, not to the whole case ([glossary](glossary.md#incident)). You need an open case that you have copied and no active run: the contents of the dock **Scenarios** are disabled during a run. Keep the scenario without them, such as `baseline`, to compare with ([Compare a scenario with a baseline](#compare-a-scenario-with-a-baseline)).

1. Choose **Editors > Scenarios**. On the tab **Scenario**, select a scenario and click **Duplicate**, or click **New** for an empty one. The new scenario is selected. Give it a **Name**, such as `Express breakdown`.
2. Open the tab **Incidents** and click **Add Incident**. Set **Type** to **Signal failure** or **Train breakdown**, choose the **Target** and enter **Start (s)**. For an incident that ends, tick **End (s)** and enter the end; a signal failure needs one. Press Enter after each number. The times are seconds after the base time of the case.
3. To take the target from the network view instead, select a signal head or a train there before you click **Add Incident**: it becomes the target of the new incident. The icon button **Pick** beside **Target** waits for the next one that you click and takes it as the target; Esc or a second click on **Pick** stops the wait. A signal head gives a signal failure of its block, or of the authored signal when exactly one protects its section, and a train of a completed run gives a breakdown of its occurrence.
4. For an entrance delay, open the tab **Entrance delays** and click **Add**. This adds a row for the first service that has a planned departure, at its first stop with one, with a delay of 0 s. With the row selected, change **Service**, **Occurrence**, **Station** and **Delay (s)**; the line in the list follows each change, and **Delay (s)** takes effect when you press Enter or leave the field.

You can also start on the network view: right-click a signal head and choose **Add signal failure**, or right-click a train after a run and choose **Add train breakdown**. The information panel of a signal or a train has the same action as a button under its fields. It adds the incident to the selected scenario as one edit, which Undo takes back, opens the tab **Incidents** with the new incident selected and puts the cursor in **Start (s)**. It is disabled while the case runs and when the item gives no target, and its tooltip says why. After a run the new incident discards the results, as any edit does, and the status bar says so.

![The dock Scenarios of Paimpol on the tab Incidents of the scenario Express breakdown: new_incident, a Train breakdown with the target Guin-Paim-EXPRESS-1-1 (route route6, +10 s), Start (s) 900, End (s) ticked with 1200, Reduced speed and Terminate at destination unticked.](../images/how-to-add-an-incident.png)

The picture shows the scenario `Express breakdown`, a duplicate of `baseline` of Paimpol, with one train breakdown of the express from 900 s to 1200 s. The [tutorial](tutorial-modify-a-case.md) adds the same incident and compares its run with `baseline`.

## Results and exports

You need a run that reached its end. Until then the result commands are disabled.

1. The dock **Run Results** opens when the run ends. If it is closed, choose its entry in the menu **View**, which starts with Run Results.
2. Click **Open result view...** and choose an entry, or use the menu **Diagrams**. An entry in **Diagrams** that asks for more input ends in an ellipsis.
3. Some views ask for a reference route or a scope first; [View a graph of a run](#view-a-graph-of-a-run) lists the views and what each asks for.
4. In a chart window, **Fit** in the header returns to the full chart. The panel at the right has **Trains**, which filters the trains, and **CSV...** and **PNG...**, which save the data and the picture of the visible trains. After a CSV export, the status bar of the main window names the saved file for a few seconds. Hover, click, zoom and pan are described in [Inspecting diagrams](diagram-navigation.md).

The buttons **Export CSV...** and **Export PNG...** in the dock save the summary table as `run_summary.csv` and `run_summary.png`. Each export asks for a path, adds the extension when you leave it off (and asks before that name replaces a file) and writes a second file, `<name>.provenance.json`, that records the run. Exports are not written to the output folder, and they are not part of the case. An export dialog starts in your Documents folder, or in your home folder when there is none. A window keeps the run it was opened for, so a later run does not change it. Creating or opening another case closes the result windows. If the write fails, the message **Export failed** names the path and, on the line below, the reason.

Delay figures are explained in [How delays are calculated](delay-analysis.md), capacity in [How capacity is analysed](capacity-analysis.md) and the text files of a run in [Run output](run-output.md).

## View a graph of a run

You need a run that reached its end ([Run a case](#run-a-case)). Each view opens in a window of its own.

1. Choose the view in the menu **Diagrams**, or click **Open result view...** in the dock **Run Results** and choose it there. Both list the views below in this order.
2. An entry of **Diagrams** that ends in an ellipsis asks for input first. For **Timetable graph...**, **Delays...** and **Train paths...**, the dialog **Reference route** lists the routes that the trains of the run used, each with its stations. Choose the route that the distance axis follows and click **OK**.
3. **Blocking time...** asks for a route, a stretch and a time range. **Capacity...** needs at least two trains on the chosen blocks: choose a route and a stretch, from station to station or from block to block, which defaults to the whole route, and check the trains. All the trains on these blocks are checked at first. **Capacity by area...** asks for a range of blocks on one route, or on two, and a period.

| View | What it shows | Asks first for |
| --- | --- | --- |
| **Timetable** | The planned and simulated times of each train at its stops, in a table | Nothing |
| **Timetable graph** | The planned and simulated stops of the trains on one route, with distance across and time downward | A reference route |
| **Delays** | The arrival delay of each train at its stops, in minutes, along one route | A reference route |
| **Delay analysis** | The delay of every call, a summary, the punctuality and the comparison with a delay baseline | Nothing |
| **Speed / distance** | The speed of each train against the distance it has covered | Nothing |
| **Speed / time** | The speed of each train against the time | Nothing |
| **Time / distance** | The distance each train has covered against the time | Nothing |
| **Tractive effort / distance** | The simulated tractive effort of each train against the distance it has covered | Nothing |
| **Train paths** | The path of every train on one route, with distance across and time downward | A reference route |
| **Blocking time** | The blocking times of the trains on a stretch of a route | A route, a stretch and a time range |
| **Capacity** | The minimum headways and the cycle of a chain of trains on a stretch | A route, a stretch and the trains |
| **Capacity by area** | The occupation of the blocks of an area by the trains | One or two block ranges and a period |

![The window Train paths with the header Paimpol / baseline · route2: the stations Guingamp, Brelidy-Plouec, Pontrieaux and Paimpol across the top, Distance (km) along the bottom and Time (hh:mm) downward from 07:10:40, a line for Guin-Paim-EXPRESS-1-1 and one for Guin-Paim-ALL-STOPS-1-1, and the panel at the right with Trains 2 of 2 and the export buttons PNG... and CSV....](../images/how-to-train-paths.png)

The picture shows **Train paths** after a run of `baseline` of Paimpol, on `route2`, the first route of the list. The line of each train runs down the window as time passes, and it is vertical where the train stands at a station. [Inspecting diagrams](diagram-navigation.md#timetable-graphs-and-train-paths) explains how to read and move such a chart, and [Results and exports](#results-and-exports) how to save it.

## Compare a scenario with a baseline

A scenario is a named set of incidents and entrance delays. **Set delay baseline** freezes a completed run without them, and **Compare delays** shows how much later or earlier each call of a run with them is. Any edit of the case clears the baseline, so build the scenario before you run the baseline.

1. Choose **Editors > Scenarios**. The list **Scenarios** holds each scenario. Click **New**, or select a scenario and click **Duplicate**, to create the scenario to test.
2. Add the incidents and entrance delays to that scenario ([Add an incident or an entrance delay](#add-an-incident-or-an-entrance-delay)).
3. Select the scenario without incidents and entrance delays (the bundled cases have one, `baseline`). Click **Run**, and **Run simulation**. When the run has ended, click **Set delay baseline** in the dock **Run Results**. The line under **Delay comparison** names the baseline.
4. Select the scenario with the incident in the list **Scenarios**. This keeps the baseline and discards the results. Run it.
5. Click **Compare delays**. The window **Delay analysis** opens on the tab **Against baseline**.

The two buttons say why they are disabled in their tooltips and in the line above them. **Set delay baseline** needs a completed run whose scenario has no incident and no entrance delay. **Compare delays** needs a baseline and a completed run whose scenario has at least one of them. The tab lists every reason when the baseline does not fit the run: the runs need different scenarios and the same case revision, base time, duration, timestep and occurrences. [How delays are calculated](delay-analysis.md#delay-comparison-against-a-baseline) has the rules. [Scenario library and student loop](scenarios.md) describes the library. **Import...** and **Export...** in the dock move a single scenario between cases.

## Change the application settings

**Edit > Settings...** opens the window **Settings**. On macOS the entry is in the **EGTRAIN** menu (Command+,). The window holds the settings that apply to every case:

- **General**: the recent cases that **File > Recent Cases** and **Open a Case** offer, and **Clear Recent Cases**.
- **Display**: **Show passengers**, the same switch as **View > Passengers**, **Show advanced details**, the same as **View > Advanced Details**, and **Station marks**, the look of the stations on the canvas and in the map key: **Map pin**, a pin that stands on the station, or **Ring and bar**, **Class marks** or **Plain dot**, which sit on the station. Plain dot tells the station classes apart by size only.
- **Updates and privacy**: **Automatically check for updates**, the same as the Help entry, and the two choices of **Help > Privacy & diagnostics...**. A build that collects nothing shows them disabled.

**OK** saves the changes, and the menu entries, the canvas and the map key show them at once. **Restore Defaults** clears the recent cases, turns every switch off and sets **Station marks** to **Map pin**; EGTRAIN then asks about automatic update checks again at the next start. It too takes effect with **OK**. The settings of the open case, such as its base time and duration, are in **Case Settings**.

## If EGTRAIN closes unexpectedly

When EGTRAIN crashes, it saves a short crash note with the version, the cause and, on macOS and Linux, where in the program it stopped. The system may show its own crash message as well. At the next start EGTRAIN says that it closed unexpectedly and where the note is: in **Recover Unsaved Work** when it has unsaved edits to offer, otherwise in the notice **EGTRAIN Closed Unexpectedly**, whose **Show Crash Note** opens the note. Each note is reported once. Attach it when you report the problem; EGTRAIN does not send it anywhere.

| System | Crash notes folder |
| --- | --- |
| macOS | `~/Library/Application Support/EGTRAIN/EGTRAIN/Crashes` |
| Windows | `%LOCALAPPDATA%\EGTRAIN\EGTRAIN\Crashes` |
| Linux | `~/.local/share/EGTRAIN/EGTRAIN/Crashes` |

EGTRAIN keeps the 10 newest notes that it has reported.

## Why a command is disabled

| Command | It is disabled when | What to do |
| --- | --- | --- |
| **Run** (toolbar), **Simulation > Run** | No case is open, a run is active, or validation reports an error | Read the first error in **View > Case Validation** ([fix validation errors](#fix-validation-errors)) or wait for the run |
| **Pause**, **Stop** | No run is active, or the run is already stopping | Start a run, or wait for the run to stop |
| **File > Save**, **Save As...** | No case is open | Open or create a case. A normal start always has one open |
| **Simulation > Set Output Folder...** | A run is active | Wait for the run to end. The folder applies from the next run |
| The contents of the editors **Services**, **Passengers**, **Infrastructure** and **Scenarios** (the menu **Editors** still opens them) | A run uses the case | Stop the run or wait. **Case Settings**, **Rolling Stock Units** and **Compositions** stay usable during a run, and an edit there discards the results of that run |
| **Edit > Undo**, **Edit > Redo** | A run uses the case, or there is nothing to undo or redo | Stop the run or wait |
| **Diagrams** entries, **Open result view...**, **Export CSV...**, **Export PNG...** in **Run Results** | No run has reached its end since the last edit | Run again. A stopped run, an edit, a new case and another scenario all discard results |
| **Set delay baseline** | There is no completed run, or its scenario has an incident or an entrance delay | [Compare a scenario with a baseline](#compare-a-scenario-with-a-baseline) |
| **Compare delays** | There is no baseline, or the completed run has neither an incident nor an entrance delay | Same |
| **Follow** | No run has started, or the case has no services | Run the case and choose a train |

When **Run** is enabled but a message refuses the run, it names the problem, for example `Select at least one occurrence on the Run occurrences tab of Services.` On Windows and Linux the message is titled **Cannot Run Case**. In the tab **Run occurrences** of **Services**, tick **Run** for at least one occurrence.
