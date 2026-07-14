# Lebanon case study guide

This guide takes you from a fresh install to a finished simulation run of the Lebanon case study. It covers the packaged application, the authoring steps you complete in the editor, the diagrams and results, and the file exports. Follow it in order for the presentation.

The Lebanon scene ships as an infrastructure shell. It contains the 34 stations and the track network. It does not contain rolling stock, compositions, services, or a timetable. You add those in the editor before the scene can run. The section [What the shell already contains](#what-the-shell-already-contains) lists exactly what is present and what you supply.

## Install and launch

The package holds the EGTRAIN application, its Qt and runtime libraries, the `scene_tool` command, the Lebanon scene, and this guide.

- macOS: open `QEGTRAIN.app`. If the system blocks the first launch, open it once from the right-click menu and choose Open.
- Windows: run `QEGTRAIN.exe` from the unpacked folder. Keep the folder intact so the application finds its libraries.
- Linux: run `./QEGTRAIN` from the unpacked folder.

The main window opens with a menu bar, the network view, and the editor docks. The menu bar holds the File, View, Simulation, Tools, Diagrams, and Help menus that the rest of this guide refers to.

## Open the Lebanon scene

Choose File > Open Scene and select the `Scenes/Lebanon` folder inside the package. The network view draws the Lebanon track layout and the 34 stations.

The docks on the right hold the editors. If a dock is hidden, turn it back on from the View menu. You will use Train Units, Compositions, Services, and Validation.

## What the shell already contains

Present in the scene:

- 34 stations, in `stations.json`.
- The track geometry for lines B0 through B7, kept under `legacy/Tracklines`. The network drawing and the simulation both read this legacy passthrough. The canonical `infrastructure.json` holds no separate geometry, so do not expect arc or node records there.

Not present, and yours to add:

- Train units and their traction curves.
- Compositions.
- Services and their timetable stops.

Until you add train and service data, Validation reports two errors: no services defined and no trains defined. That is expected for the shell.

## Add train units and traction curves

Open the Train Units dock.

1. Select Add Train Unit. A new unit appears in the list.
2. Give the unit an id.
3. Fill the physical fields: mass, length, and the other values for the stock you are modelling.
4. Enter the traction curve rows. Each row is a speed interval with a lower speed, an upper speed, and the three coefficients C0, C1, and C2. Tractive effort at a speed follows C0 plus C1 times speed plus C2 times speed squared, in newtons, with speed in metres per second.
5. If you imported the unit from a legacy `LITRA` and `T_LITRA` pair, the Source data file and Source traction file fields show those names.

Repeat for every unit the presentation needs.

## Build compositions

Open the Compositions dock.

1. Select Add Composition and give it an id.
2. With the composition selected, use Add Unit to attach train units in order. Use Move Up and Move Down to set the order.
3. Select a unit in the composition to see its rolling-stock data file and traction data file. Select Plot Traction Curve to open the tractive-effort plot for that unit. The plot labels the speed and effort axes and names the source traction file.
4. If a unit is missing, has no traction curve, or has a curve without a recorded traction file, the panel shows a warning. Fix the unit before you rely on the run.

## Add services and timetable stops

Open the Services dock.

1. Select Add Service and give it an id.
2. Choose the composition and the route for the service.
3. Set the entry time. Set a repeat headway if the service runs more than once.
4. In the stops list, add each station the service calls at. For every stop, set the planned arrival, planned departure, and dwell as the timetable requires.

## Read and fix validation errors

Open the Validation dock. It lists every problem in the current scene with a code, the file it comes from, and a suggested fix. Work down the list until it is clear. Common cases:

- A service points at a composition or route that does not exist. Fix the reference or add the missing record.
- A stop names a station that is not on the route.
- A train unit has a traction curve whose speed intervals overlap or leave a gap.

Run stays gated while errors remain.

## Save a working copy

Choose File > Save Scene As and pick a folder outside the package. This writes your edits to a separate copy and leaves the supplied Lebanon scene unchanged. Use File > Save Scene, or Ctrl+S, to save later edits to that working copy.

Do not save into the package folder. Keep the supplied scene as your clean starting point.

## Run the simulation

Choose Simulation > Start, or press Ctrl+R. The progress bar shows the run. Use Simulation > Pause and Simulation > Stop to control it.

When the run finishes, the status bar reads Simulation complete and the Run Results dock fills in.

## Open the diagrams

The Diagrams menu opens each chart after a run:

- Speed / Distance (per train)
- Speed / Time (per train)
- Time / Distance (per train)
- Timetable graph (train graph)
- Blocking-time overlay
- Timetable table (planned vs simulated)
- Train delays

Tools > Train Path Diagrams opens the corridor train path diagram.

Every chart window has a train panel on the right. Use the search box and the checkboxes to show or hide trains. Hover a line to read its train id. Click a line to select it, which fades the others and centres the network view on that train. Use All, None, and Clear selection to reset. Zooming and panning keep the filter.

## Read travel time and energy

The Run Results dock lists, for each train, the start time, end time, travel time, and the four energy measures. The last row holds the network totals. Turn the dock on from the View menu if it is hidden.

## Export images and data

Every diagram window and the Run Results dock export raw data.

- Export PNG saves the current chart, with its zoom and visible trains, as an image. The file dialog adds the `.png` extension if you leave it off.
- Export CSV saves the data behind the view. The trajectory export holds time, position, speed, power, cumulative energy, and the occupied block for each train. The timetable export holds planned and simulated arrival and departure and the delays. The run summary holds the start, end, travel time, and energy totals. The blocking-time export holds each occupied block with its start, end, position, and type.

CSV files use a comma separator, a header row, a decimal point, and an empty field for a missing value. They open in Excel, LibreOffice, Python, and R. Save exports to your working folder, not into the package.

## Find the output and recover from a failed run

Choose File > Set Output Folder to pick where the run writes its text output. The per-train trajectory files and the energy files land there.

If a run stops with an error:

1. Read the message in the status bar and the log pane. Turn the log pane on from the View menu.
2. Open the Validation dock and clear any error it reports.
3. Check that every service has a composition with at least one train unit, and that every unit has a valid traction curve.
4. Save the scene and start the run again.

## Command reference

`scene_tool` runs the same import, validate, and export steps without the window. Run it from the package folder.

```
scene_tool import <legacyDir> <sceneDir> [sceneName]
scene_tool validate <sceneDir>
scene_tool export <sceneDir> <outDir>
```

- `import` builds a scene from a legacy case folder. The Lebanon scene was built this way from the supplied network.
- `validate` prints the same errors the Validation dock shows.
- `export` writes the legacy files a simulation run reads, including the B0 through B7 track passthrough, into a clean directory.

## Rehearsal checklist for 15 July

Run this once on the presentation machine before the session.

- [ ] The package launches with no missing-library error.
- [ ] File > Open Scene loads `Scenes/Lebanon` and the network draws.
- [ ] The train units, compositions, services, and timetable for the presentation are authored and saved to a working copy.
- [ ] The Validation dock is clear.
- [ ] Simulation > Start completes and the status bar reads Simulation complete.
- [ ] Every diagram in the Diagrams menu opens with readable data.
- [ ] The Run Results dock shows travel time and energy per train and the network totals.
- [ ] One PNG and one CSV export open correctly outside the package.
- [ ] The supplied `Scenes/Lebanon` folder is unchanged; edits live in the working copy.
