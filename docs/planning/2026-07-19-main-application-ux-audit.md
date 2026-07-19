# Main application UX audit

Date: 2026-07-19

## Scope and evidence

This audit covers the main playback workspace, the startup chooser, and the controls that remain visible during a simulation. It uses the Copenhagen case at the 1200 by 800 logical window size exercised by `tools/e2e/visual_polish_smoke.sh`. The default, dense, follow-train, and context-menu captures all passed the smoke test on commit `8abde7e`.

The smoke test proves that the controls and scene items exist. It does not prove that they are readable. The captures reproduce label collisions, clipped text, toolbar crowding, a map-obscuring legend, weak zoom control, and an unreadable timestep bar.

## Findings

### P1: station labels collide with symbols and clip at the viewport edge

At the default fit, station symbols cover parts of labels such as Taastrup and Ballerup. Labels near the right edge are cut to a few characters. The dense capture separates some labels, but the result changes with zoom rather than following a stable label rule.

`MainWindow::paintStationName()` creates fixed-pixel text with `ItemIgnoresTransformations`, while `paintStationIcon()` creates a separate fixed-pixel icon. Their scene-space anchors are calculated independently in `EGTRAIN/QEGTRAIN/app/MainWindow.cpp:6262` and rendered in `EGTRAIN/QEGTRAIN/app/MainWindow.cpp:7097` and `EGTRAIN/QEGTRAIN/app/MainWindow.cpp:7112`. The icon-to-label gap therefore shrinks or grows with the scene transform even though both items keep a fixed screen size.

Students cannot reliably identify stations in the view where they are expected to follow trains and routes.

### P1: legacy label offsets distort fit-to-view and create dead space

The Copenhagen renderer contains station-name lists with Y offsets of 900, 920, 2200, 2800, and 3500 scene units in `EGTRAIN/QEGTRAIN/app/MainWindow.cpp:6278`. `MainWindow::fitView()` includes those decorations when it calculates the scene bounds in `EGTRAIN/QEGTRAIN/app/MainWindow.cpp:8506`. The resulting default view leaves large empty regions above and below the useful track geometry while rendering the tracks thin and compressed.

The map spends screen area on historical positioning corrections instead of the railway topology.

### P1: network zoom has no lower or upper bound

Wheel zoom, toolbar zoom, and `NetworkView::scale()` multiply the current transform without a limit. The paths are in `EGTRAIN/QEGTRAIN/app/MainWindow.cpp:7708`, `EGTRAIN/QEGTRAIN/app/MainWindow.cpp:9780`, and `EGTRAIN/QEGTRAIN/graphics/NetworkView.cpp:24`. Repeated input can make the scene effectively disappear or magnify it until navigation becomes impractical. The UI shows no zoom level and gives no feedback when fit-to-view is restored.

Users can lose the network and have to know that Ctrl+0 restores it.

### P1: the timestep bar is mathematically wrong and too short for its text

`TimeProgressBar::setProgress()` divides two integers before multiplying by 100. The value remains zero for almost the entire run. The widget is fixed at 10 pixels high while it tries to draw `Time: 9 / 7999`. See `EGTRAIN/QEGTRAIN/widgets/TimeProgressBar.cpp:4` and `EGTRAIN/QEGTRAIN/widgets/TimeProgressBar.cpp:15`.

The same moment also appears as a toolbar clock and as `Simulation running HH:MM:SS / HH:MM:SS` in the status bar at `EGTRAIN/QEGTRAIN/app/MainWindow.cpp:8685`. Three displays compete, but none clearly explains the relationship between clock time, timestep, and total progress.

Progress appears stalled. Students have to infer whether the numbers are seconds, steps, or clock time.

### P1: the command bar is crowded and breaks action grouping

The 1200-pixel toolbar contains a case badge, Run, Pause, a large clock, speed text and slider, Follow and a 180 to 260 pixel train selector, Start Time, Stop, Zoom In, and Zoom Out. Fit View is not visible in the smoke capture. Stop is separated from Run and Pause because several widgets are inserted relative to the Stop action in `EGTRAIN/QEGTRAIN/app/MainWindow.cpp:628` through `EGTRAIN/QEGTRAIN/app/MainWindow.cpp:785`.

The clock, case badge, and Follow state use large filled rectangles, so secondary state has similar weight to the run controls. Text-beside-icon treatment makes every action wide.

The primary sequence of open, run, pause, and stop is harder to scan. Actions fall into the toolbar overflow at the supported default size.

### P2: the permanent legend covers the network

`NetworkLegendItem` is a fixed 190 by 154 pixel overlay in `EGTRAIN/QEGTRAIN/graphics/items/NetworkLegendItem.cpp:7`. `updateViewportOverlays()` pins it to the lower-right corner in `EGTRAIN/QEGTRAIN/app/MainWindow.cpp:10259`. It has no close, collapse, or View-menu action. In both default and dense captures it occupies useful map area and can sit over tracks.

The left Case and Layers dock has enough unused vertical space to hold a compact, collapsible map key without covering the canvas.

### P2: station-name culling is order-dependent and silent

`updateViewportOverlays()` walks `m_stationNames` and keeps the first non-overlapping label in `EGTRAIN/QEGTRAIN/app/MainWindow.cpp:10217`. There is no priority for interchanges, network endpoints, the selected station, or the station nearest a followed train. Hidden labels have no hover or selection fallback.

A less useful label can hide a more useful one, and the user cannot tell that another station is present.

### P2: signals dominate the topology at dense zoom

The dense capture renders a signal housing at nearly every block boundary. Their bright green aspects and repeated dark housings draw more attention than tracks, switches, stations, and the selected train. `kDenseDetailScale` switches all signals at one threshold in `EGTRAIN/QEGTRAIN/app/MainWindow.cpp:57` and `EGTRAIN/QEGTRAIN/app/MainWindow.cpp:10215`.

Zooming in increases detail but reduces the visual hierarchy of the operational map.

### P2: track state relies too heavily on color

Prepared and occupied sections are both solid lines. They differ mainly by green versus red and by line width. Blocked sections add a dash pattern. See `EGTRAIN/QEGTRAIN/graphics/VisualPolish.cpp:33` and the matching legend in `EGTRAIN/QEGTRAIN/graphics/items/NetworkLegendItem.cpp:9`.

Red and green are also reserved for signal aspects. Reusing them for route state makes the scene harder to teach and gives users with red-green color-vision deficiency too little redundant information.

### P2: train identity truncates at the point of use

The default and follow captures show the selected train badge as `H-Bal...`, while the toolbar selector displays the full `H-Ballerup-Osterport-1` identifier. The canvas badge is the object users follow, but it omits the distinguishing part of the name. `paintTrainBadge()` assigns the full description and the badge decides how much to render in `EGTRAIN/QEGTRAIN/app/MainWindow.cpp:7680` and `EGTRAIN/QEGTRAIN/graphics/items/TrainBadgeItem.h:13`.

Users must look away from the train to identify it.

### P2: layer names describe implementation details

The left dock uses `Station decorations`, `Trains with speed labels`, `Signal groups / aspects`, and `Passenger load` in `EGTRAIN/QEGTRAIN/app/MainWindow.cpp:668`. The first three combine multiple visual layers under one checkbox. The custom checkbox style draws a filled teal square for checked state without a conventional check mark in `EGTRAIN/QEGTRAIN/resources/styles/egtrain.qss:45`.

It is unclear whether a control hides an object, its label, or both.

### P2: playback speed has no visible direction or range

The speed slider reports only the current mode, such as `Speed: fastest`. It does not label either end of the track. The code correctly maps higher slider values to shorter delays in `EGTRAIN/QEGTRAIN/app/MainWindow.cpp:65` and creates the unlabeled control at `EGTRAIN/QEGTRAIN/app/MainWindow.cpp:508`.

Users have to move the control to learn which direction is faster. The intended direction should be visible before interaction.

### P2: the train context menu exposes an unavailable future action

The train context menu ends with a disabled `Planned route and related infrastructure` item. It makes the compact menu much wider than the four working commands and looks like a broken feature. The item is added at `EGTRAIN/QEGTRAIN/app/MainWindow.cpp:3827` and is visible in the context-menu smoke capture.

Unavailable future commands should not occupy the working interface. Add the route action when the train-to-route association exists.

### P2: case and simulation state are duplicated

`Case: Copenhagen` appears in the toolbar and `Copenhagen` appears again in the left dock. Current time appears in the toolbar and status bar. The status bar is also used for transient guidance and errors, so continuous simulation text overwrites those messages.

Duplicated state consumes space without adding context, while transient feedback is easy to miss.

### P2: canvas navigation is not discoverable

The network view supports drag-to-pan, wheel zoom, Ctrl-modified scrolling, toolbar zoom, fit-to-view, selection, and context menus. The canvas shows none of these controls or shortcuts. The relevant behavior is split between `EGTRAIN/QEGTRAIN/graphics/NetworkView.cpp:5`, `EGTRAIN/QEGTRAIN/app/MainWindow.cpp:7708`, and `EGTRAIN/QEGTRAIN/app/MainWindow.cpp:9780`.

First-time users have to experiment or read source-level documentation to learn basic navigation.

### P2: leaving the startup chooser creates an unexplained blank state

The startup chooser offers Open, Open Scene Folder, Import Legacy Case, and Skip in `EGTRAIN/QEGTRAIN/app/MainWindow.cpp:6577`. Skip closes the dialog and leaves the main canvas without an in-canvas next step. The only guidance is a temporary status-bar message.

A student who dismisses the chooser can land in an empty workspace with no persistent call to action.

## Design direction

Three approaches were considered.

1. A QSS-only polish would reduce padding and adjust colors quickly. It would not fix station geometry, zoom behavior, progress math, or overlay obstruction.
2. A staged Qt Widgets revision fixes the shared interaction paths first, then adjusts the shell and renderer. It keeps `QMainWindow`, `QToolBar`, `QDockWidget`, `QGraphicsView`, and the current scene items. This is the recommended approach.
3. A new custom canvas and workspace shell could produce the cleanest result, but it would replace working Qt patterns and carry more simulation and scene-selection risk than the current problems justify.

The target is a railway operations teaching desk. It should be compact, calm, and readable at 1200 by 800. The railway state carries the visual emphasis. Widget chrome stays neutral.

### Layout

- Divide the command bar into Case, Playback, and View groups with narrow separators. Put Open Case in the first group; keep Run, Pause, Stop, and speed together in Playback; keep Follow, zoom, and Fit in View.
- Remove the duplicate case badge and clock from the command bar.
- Keep speed and follow controls compact. Move Start Time into simulation setup or the Simulation menu.
- Use icon-only Zoom In and Zoom Out actions with tooltips. Keep Fit visible as the recovery action.
- Replace the 10-pixel progress bar with one 28-pixel read-only simulation timeline below the canvas. It shows current clock time, `Step n of total`, and end time in one line. Keep the progress fill, but do not give it focus, a pointing cursor, click handling, or any other scrubbing cue.
- Move the map key into the unused lower part of the left dock. Use compact 160-pixel rows, start expanded for a new profile, and keep the Map key header visible when collapsed.
- Use the left dock for case name, run readiness, layers, and the map key. Do not add decorative cards to fill space.

### Network view

- Calculate fit-to-view from topology, not fixed-size labels, icons, badges, or the legend.
- Anchor each station symbol and label as one screen-space overlay with a fixed gap.
- Keep labels inside viewport padding. Below 2x, show selected, followed-train, interchange, and network-endpoint labels. Define a network endpoint from the rendered infrastructure node degree, not from service assumptions. From 2x up to but excluding 4x, add ordinary station labels when they do not collide. At 4x and above, try every label and resolve remaining ties by selection first, station class second, and distance from the viewport center third.
- Reveal a culled station name on hover or selection.
- Treat fitted view as the map baseline. Clamp zoom from Fit to 12x and show map-style labels such as `Fit`, `2x`, and `12x`, not document percentages.
- Use progressive signal detail rather than switching every signal housing at one threshold.

### Color and type

The shell uses neutral railway-instrument colors rather than the current teal accent:

| Role | Color |
| --- | --- |
| Shell background | `#F2F3F1` |
| Panel background | `#FAFAF8` |
| Main text | `#24313A` |
| Divider | `#C8CED2` |
| Primary action and focus | `#315A70` |
| Secondary focus or warning | `#965C22` |
| Network canvas | `#12191F` |
| Network text | `#E6EAEC` |

Use a 1-pixel `#3A4A54` inset border around the network view. The border makes the light workbench shell and dark map surface read as one deliberate instrument panel without adding shadows or card styling.

Track state uses both color and stroke:

- Free: `#A0ACB4`, 2-pixel solid line.
- Prepared route: `#4C8DAE`, medium dash-dot line.
- Occupied: `#D05A47`, heavy solid line.
- Blocked: `#D6A13A`, heavy dashed line with a block marker.

These use Qt pen styles rather than custom track textures. State remains legible through color, width, and dash pattern without adding visual noise to dense junctions.

Signals keep standard red, amber, and green aspects because those colors have railway meaning. Their housing shape and aspect icon remain visible without color. Train categories keep their existing shape differences and move to less saturated fills.

Use the platform system font for controls and labels. Use the platform fixed-width font for the simulation clock and timestep only. This avoids a font dependency and keeps numerical changes from shifting the layout.

## Success criteria

- No station name intersects its own station symbol at Fit, 3x, or 12x zoom.
- Important station labels stay within 12 pixels of the viewport edge and remain discoverable when collision culling hides them.
- Wheel and toolbar zoom cannot exceed the documented range.
- The simulation timeline advances on every reported timestep and explains clock time, current step, total steps, and end time.
- Open, Run, Pause, and Stop remain visible and adjacent at 1024 by 720, 1200 by 800, and 1440 by 900.
- The speed control visibly identifies its slower and faster ends, with faster on the right.
- The map key never covers track geometry.
- Context menus contain working commands only and do not widen for unavailable placeholders.
- Prepared, occupied, and blocked track states remain distinguishable in grayscale and with common red-green color-vision deficiencies. Signal icons remain distinguishable without aspect color at minimum zoom.
- Keyboard focus is visible on every command-bar and layer control.
- Visual smoke coverage records default, dense, follow, narrow-window, and maximum-zoom states.
