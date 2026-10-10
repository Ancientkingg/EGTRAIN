# Lebanon case study guide

This guide takes you from a fresh install to a finished simulation run of the Lebanon case study. It covers the packaged application, the authoring steps you complete in the editor, the diagrams and results, and the file exports. Follow it in order for the presentation.

The Lebanon scene ships as a runnable teaching baseline. It contains the 34
stations, the track network, one short route, and one example service. Run that
baseline first, then save a working copy and replace the example operating data
with the case you want to study.

## Install and launch

Download the package for your system from the [releases page](https://github.com/Ancientkingg/EGTRAIN/releases); the [quick start](https://github.com/Ancientkingg/EGTRAIN#quick-start) in the README describes the download and the start.

The macOS and Windows packages hold the EGTRAIN application, its Qt and runtime libraries, the `scene_tool` command, the Lebanon scene, and this guide.

- macOS: unzip `QEGTRAIN-macos-arm64.zip`. It unpacks to the folder `QEGTRAIN-Lebanon`, which holds `QEGTRAIN.app`, `scene_tool`, `Scenes/Lebanon`, and this guide. Open `QEGTRAIN.app`. If the system blocks the first launch, open it once from the right-click menu and choose Open.
- Windows: unzip `QEGTRAIN-windows-x64.zip` into a new, empty folder and run `QEGTRAIN.exe` from it. Keep the folder intact so the application finds its libraries. The Windows files are not code signed, so Windows may show a warning before the first start.
- Linux: the download is the single file `QEGTRAIN-linux-x86_64.AppImage`. Make it executable and run it. There is no unpacked folder and no package folder, so open Lebanon from the file `Lebanon.egscene`, as the next section describes.

The main window opens with a menu bar, a toolbar, and the network view. The menus used in this guide are File, View, Editors, Simulation, Diagrams, and Help.

## Open the Lebanon scene

Every time EGTRAIN is started as the section Install and launch describes, it shows the window Open a Case over the case it has loaded already. Continue closes it. At the first start only, EGTRAIN may then ask whether it may check for updates automatically. Either answer works, and the Help menu entry Automatically Check for Updates changes it later.

Choose File > Open Scene Folder... and select the `Scenes/Lebanon` folder of the package. The `Scenes` folder lies next to `QEGTRAIN.app` on macOS and next to `QEGTRAIN.exe` on Windows. The network view draws the Lebanon track layout and the 34 stations. If you downloaded `Lebanon.egscene` from the releases page, choose File > Open Case Study... and select that file instead. On Linux there is no package folder, so open the scene from that file.

Before you change anything, save a working copy, as [Save a working copy](#save-a-working-copy) describes.

The editors are docks on the right side of the window. They are hidden when the window opens. The Editors menu opens them: Case Settings, Infrastructure, Rolling stock units, Compositions, Services, Incidents, and Passengers. The Scene Validation dock, the Console Log, and the Run Results dock are in the View menu. You will use Rolling stock units, Compositions, Services, and Scene Validation.

## What the baseline contains

Present in the scene:

- 34 stations, in `stations.json`.
- Canonical track geometry for lines B0 through B7, in `infrastructure.json`. The network preview reads these nodes, arcs, connections, and platform anchors directly.
- A short B0 teaching route between Lebanese University and Haret Hreik.
- One network-wide signalling area at level 0, in `signalling.json`: placeholder conventional fixed-block signalling, not the national system.
- One teaching train unit and composition, one `LB-1` service, and a baseline scenario.

The example train parameters reuse EGTRAIN's existing Assignment SLT parameters
only to make the former infrastructure shell runnable. They are not claimed as
Lebanon rolling-stock data and carry no Lebanon source provenance. Replace them
before using the scene for engineering conclusions.

All simulation input is canonical JSON. No hidden `legacy/` files are required
by preview or simulation.

## Add train units and traction curves

Open the Rolling stock units dock.

1. Select Add Unit. A new unit appears in the list.
2. Give the unit an id in the field Rolling stock unit ID.
3. Fill the physical fields under Rolling stock unit characteristics: mass, length, and the other values for the stock you are modelling.
4. Select Add Traction Row for each row of the traction curve and fill in the new row. Each row is a speed interval with a lower speed, an upper speed, and the three coefficients C0, C1, and C2. Tractive effort at a speed follows C0 plus C1 times speed plus C2 times speed squared, in newtons, with speed in metres per second.
5. **Parameter source reference** and **Tractive-effort source reference** retain imported filenames and accept references for newly authored units. Provenance is optional and is not reopened by the runtime.

Repeat for every unit the presentation needs, then delete the teaching unit with Delete if no composition uses it.

## Build compositions

Open the Compositions dock.

1. Select Add Composition and give it an id.
2. With the composition selected, use Add Unit to attach train units in order. Use Move Up and Move Down to set the order.
3. Select a unit in the composition to see its original parameter and tractive-effort sources. Select **Plot input traction characteristic** to open the curve for that unit. The plot displays speed in km/h, effort in kN, and names the source file when provenance exists.
4. If a unit is missing or has no tractive-effort curve, the panel shows a warning. Fix the unit before you rely on the run.

## Add services and timetable stops

Open the Services dock.

1. Select Add Service and give it an id.
2. Choose the composition and the route for the service.
3. Select the check box Entry Time (s) and enter the entry time. Select the check box Repeat Headway (s) and enter the headway if the service runs more than once.
4. Select Add Stop for each station the service calls at. In the window Add timetable stop, set the station, the planned arrival, the planned departure, and Minimum dwell (s) as the timetable requires. Planned time display switches the times between Elapsed offsets (s) and Clock time. The elapsed seconds count from the base time of the case, 06:00:00 for Lebanon.

## Read and fix validation errors

Open the Scene Validation dock. It lists every problem in the current scene with its severity, a code, the file and path it comes from, and a suggested fix. The columns are Severity, Code, Message, File, Path, and Suggested Fix. Work down the list until it is clear. Common cases:

- A service points at a composition or route that does not exist. Fix the reference or add the missing record.
- A stop names a platform that the route does not reach, or the stops are out of route order. A stop without a platform at a station outside the route is only a warning.
- A train unit has no traction rows, or its traction rows overlap, are out of order, or have a lower speed that is not below the upper speed.

Run stays gated while errors remain.

## Save a working copy

Choose File > Save Case Study As... and write `Lebanon-working.egscene` outside the package. This leaves the supplied Lebanon scene unchanged. Use File > Save Scene, or Ctrl+S, to save later edits to that working copy. Use File > Save Scene As Folder... only when you need editable JSON files.

Do not save into the package folder. Keep the supplied scene as your clean starting point.

## Run the simulation

Choose Simulation > Run, press Ctrl+R, or select Run on the toolbar. The Run simulation window shows what the run covers; choose Run simulation to start it. The progress bar shows the run. Use Simulation > Pause and Simulation > Stop to control it. Closing the window or opening another case during a run stops the run first.

When the run finishes, the status bar reads Simulation complete - open the Diagrams menu for results, and the Run Results dock fills in.

## Open the diagrams

The Diagrams menu opens each chart after a run:

- Speed / Distance (per train)...
- Speed / Time (per train)...
- Time / Distance (per train)...
- Simulated tractive effort / Distance (per train)...
- Timetable graph (planned vs simulated stops)...
- Blocking-time overlay...
- Capacity analysis...
- Timetable table (planned vs simulated)...
- Train delays...
- Train paths (simulated movement)...

Timetable graph and Train paths first ask for a reference route in a window called Reference route. Blocking-time overlay first asks for the blocking-time scope. Capacity analysis needs at least two trains that share the entry of a section. The supplied scene has one service and so one train, so Capacity analysis shows a message and no result. A result for a larger scene also needs a chain of occurrences without overlap.

Every chart window has a Trains button at the top. It opens a list with a search box, the buttons All and None, and a checkbox for each train. Use the search box and the checkboxes to show or hide trains. Hover a line to read its train id. Click a line to select it, which fades the others and centres the network view on that train. Use All, None, and Clear selection to reset. Zooming and panning keep the filter.

## Read travel time and energy

The Run Results dock lists, for each train, the start time, end time, travel time, and the four energy measures. The last row holds the network totals. Turn the dock on from the View menu if it is hidden.

## Export images and data

Every diagram window and the Run Results dock export raw data.

- Export PNG saves the current chart, with its zoom and visible trains, as an image. The file dialog adds the `.png` extension if you leave it off.
- Export CSV saves the data behind the view. The trajectory export holds time, position, speed, power, cumulative energy, and the occupied block for each train. The timetable export holds planned and simulated arrival and departure and the delays. The run summary holds the start, end, travel time, and energy totals. The blocking-time export holds each occupied block with its start, end, position, and type.

CSV files use a comma separator, a header row, a decimal point, and an empty field for a missing value. They open in Excel, LibreOffice, Python, and R. Save exports to your working folder, not into the package.

## Find the output and recover from a failed run

Choose File > Set Output Folder to pick where the run writes its text output. The energy files, the station statistics and the train service path diagram land there. The folder applies from the next run, and the command is unavailable while a run is active.

If a run stops with an error:

1. Read the message in the status bar and the log pane. Turn the log pane on with View > Console Log.
2. Open the Scene Validation dock and clear any error it reports.
3. Check that every service has a composition with at least one train unit, and that every unit has a valid traction curve.
4. Save the scene and start the run again.

## Command reference

`scene_tool` runs the import, validate, and export steps without the window, and it packs and unpacks bundles. The macOS and Windows packages hold it next to the application. The [command-line page](https://github.com/Ancientkingg/EGTRAIN/blob/main/docs/guides/command-line.md#scene_tool) lists its commands and exit codes. Run it from the package folder:

```bash
./scene_tool validate Scenes/Lebanon
```

On Windows the command is `scene_tool.exe`. `validate --runnable` prints the errors the Scene Validation dock shows, which include the checks the application makes before a run. Without `--runnable` it leaves those checks out.

## Before you present

Run this once on the presentation machine before the session.

- [ ] The package launches with no missing-library error. Note the Open a Case window and, at the first start, the question about update checks if it appears.
- [ ] The Lebanon scene opens, from `Scenes/Lebanon` or from `Lebanon.egscene`, and the network draws.
- [ ] The supplied teaching baseline runs once without edits.
- [ ] A working copy is saved with File > Save Case Study As... before the first edit.
- [ ] The presentation rolling stock units, compositions, services, and timetable stops are authored and saved to the working copy.
- [ ] The Scene Validation dock is clear.
- [ ] Simulation > Run completes on the working copy and the status bar reads Simulation complete.
- [ ] Every diagram you present opens with readable data.
- [ ] The Run Results dock shows travel time and energy per train and the network totals.
- [ ] One PNG and one CSV export open correctly outside the package.
- [ ] The supplied scene (the `Scenes/Lebanon` folder or `Lebanon.egscene`) is unchanged; edits live in `Lebanon-working.egscene`.
