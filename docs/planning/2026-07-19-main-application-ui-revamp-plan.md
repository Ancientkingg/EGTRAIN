# Main application UI revamp plan

## Goal

Make the main EGTRAIN workspace readable and predictable without replacing Qt Widgets, the scene model, or the simulation rendering flow.

The work is split into small pull requests. Each pull request leaves the application usable and has its own regression gate. The order fixes correctness and navigation before changing the palette.

## Constraints

- Keep Qt 5 and C++17.
- Preserve legacy case loading and all four committed case studies.
- Keep `QMainWindow`, `QToolBar`, `QDockWidget`, `QGraphicsView`, and the existing scene ownership model.
- Do not add a UI framework, font package, or runtime dependency.
- Use color as a secondary cue. Track and train states need a stroke, pattern, icon, or shape cue as well.
- Keep public UI text short, literal, and consistent.
- Verify every pull request at 1024 by 720, 1200 by 800, and 1440 by 900 logical pixels.
- Treat all documented pixel dimensions as logical pixels. Verify captures at 1x and 2x device-pixel ratios. Do not add explicit high-DPI attributes where the platform already provides the correct scaling.
- Each Qt widget test must create `QApplication`, link `Qt5::Core`, `Qt5::Gui`, and `Qt5::Widgets`, and run under CTest with `QT_QPA_PLATFORM=offscreen`.

## Pull request 1: lock down viewport and timeline behavior

### Files

- Modify `EGTRAIN/QEGTRAIN/graphics/NetworkView.h` and `NetworkView.cpp`.
- Modify `EGTRAIN/QEGTRAIN/widgets/TimeProgressBar.h` and `TimeProgressBar.cpp`.
- Modify `EGTRAIN/QEGTRAIN/app/MainWindow.h` and `MainWindow.cpp` only where they route zoom and timestep updates.
- Add `EGTRAIN/QEGTRAIN/tests/test_networkview.cpp`.
- Add `EGTRAIN/QEGTRAIN/tests/test_timeprogressbar.cpp`.
- Modify `CMakeLists.txt` to register both tests.

### Work

1. Add a fitted-scale baseline to `NetworkView`. Define Fit as the transform that shows all rendered track and connection geometry inside logical-pixel padding. Do not reapply the track-separation minimum when it would crop the topology. Hide both scrollbars at Fit.
2. Keep one `NetworkView::wheelEvent()` handler and route it, toolbar actions, and mouse-anchored zoom through one `zoomBy(qreal factor)` path. Remove the parallel wheel branch from `MainWindow::eventFilter()` and the direct `QGraphicsView::scale()` call.
3. Clamp the scale from Fit through 12x. Ignore further input at either bound while still emitting one viewport update for the applied change. Expose map-style labels such as `Fit`, `2x`, and `12x` rather than percentages.
4. Recompute the fitted baseline when a case loads, when Fit runs, and when a fitted view resizes. A resize must preserve the current zoom ratio when the user has zoomed away from fit instead of snapping back unexpectedly.
5. Keep Ctrl+0 as Fit and make its tooltip state that it restores the full network view.
6. Pass `snapshot->totalTimesteps` into `TimeProgressBar` instead of reading `initial_variables.times`. Give it the range `0` through `totalSteps - 1`, and set its value directly to the zero-based timestep. Display steps as one-based values. Calculate the final interval horizon from `totalSteps`.
7. Increase the timeline height to 28 pixels and format it as `06:28:29 | Step 10 of 8,000 | Ends 08:41:40`. Keep it read-only and unfocusable, with no pointing cursor or click handling.
8. Write `Simulation running` to the status bar only when the run enters that state. Timestep updates must not touch the status bar, so errors and transient guidance remain visible for their requested duration.

### Regression checks

- `test_networkview` records the final fitted scale, applies 100 zoom-in and zoom-out operations, and asserts clamping at 12x and Fit with a floating-point epsilon. It also asserts that both scrollbars are hidden at Fit.
- Dispatch real `QWheelEvent` instances to the viewport and trigger the toolbar actions. Prove that both routes use the same clamp and that one wheel step applies once.
- At each viewport size, the test records Fit, zooms to 3x, resizes to the next supported size, and asserts that the transform remains 3x relative to the recomputed fitted baseline. It then verifies that the new baseline still clamps from Fit through 12x.
- `test_timeprogressbar` checks the first, middle, and last steps. It asserts the zero-based progress value, one-based displayed step, final-interval end clock, and formatted text. It also proves that mouse input does not change the value.
- An in-app hook posts a timed status message, advances the snapshot, and proves that the progress update does not overwrite the message.
- Run `cmake --build build`, `ctest --test-dir build --output-on-failure`, and `tools/e2e/visual_polish_smoke.sh`.

## Pull request 2: make station overlays fit-safe and collision-aware

### Files

- Add `EGTRAIN/QEGTRAIN/graphics/items/StationOverlayItem.h` and `StationOverlayItem.cpp`.
- Modify `EGTRAIN/QEGTRAIN/app/MainWindow.h` and `MainWindow.cpp`.
- Modify `EGTRAIN/QEGTRAIN/graphics/NetworkView.cpp` only if viewport padding needs a shared helper.
- Add `EGTRAIN/QEGTRAIN/tests/test_stationoverlay.cpp`.
- Extend `tools/e2e/visual_polish_smoke.sh` and its in-app capture assertions.

### Work

1. Replace the separate station icon and rich-text label with one fixed-screen overlay item. Paint the symbol and plain label in one bounding rectangle with an 8-pixel gap. Accept hover events, set accepted mouse buttons to `Qt::NoButton`, and do not handle context-menu events.
2. Keep existing station classification and icons. Do not change scene or simulation data.
3. Compute topology bounds by direct typed iteration over `TrackLineItem`, including `VirtualArcItem`, and `ConnectionItem`. Union painted line geometry, not the wider selection polygons returned for hit testing. Do not use a bounds cache or subtract overlays from an all-items union.
4. Add 24 logical pixels of fit padding around topology.
5. Remove scene-space icon-to-label offsets. Preserve case-specific station anchors only until a later scene-format issue can replace them with view metadata.
6. In the viewport pass, evaluate label rectangles with 4 logical pixels of collision padding. Below 2x, consider selected, followed-train, interchange, and network-endpoint labels. The followed-train station is the station anchor nearest the followed train's current scene position. Define a network endpoint as a station whose rendered infrastructure node has one connection. Do not infer service termini. From 2x up to but excluding 4x, add ordinary labels that fit. At 4x and above, consider every label.
7. Resolve a remaining collision in this order: selected station, followed-train station, interchange, degree-1 network endpoint, remaining station class, then distance from the viewport center. Do not depend on source order.
8. Keep a stable preferred label anchor and shift the label within the 12-pixel viewport inset. Use a deterministic alternate side only when the preferred side cannot fit.
9. Show the full name for a culled label when its station symbol is hovered or selected. Keep using the existing viewport update pass; do not add a scheduler or cache.

### Regression checks

- `test_stationoverlay` checks the combined bounding rectangle, the fixed 8-pixel symbol gap, edge placement, and priority ordering with a degree-1 network endpoint and a degree-3 interchange fixture.
- Dispatch hover and selection events to a station whose label was culled. Assert that the full name appears, then disappears when neither state applies. Assert that a click and context-menu request still resolve through the existing semantic item path.
- The visual smoke adds assertions that no visible station label intersects its own symbol and that each visible label remains inside the viewport inset.
- Run label-in-bounds and symbol-intersection checks for Netherlands, Paimpol, Copenhagen, and Brescia at Fit, 3x, and 12x. Capture the three states for Copenhagen at 1x and 2x device-pixel ratios.
- Run the full UI verification gate.

## Pull request 3: simplify the command bar and simulation timeline

### Files

- Modify `EGTRAIN/QEGTRAIN/app/MainWindow.ui`.
- Modify the command-bar construction in `EGTRAIN/QEGTRAIN/app/MainWindow.cpp` and declarations in `MainWindow.h`.
- Modify `EGTRAIN/QEGTRAIN/resources/styles/egtrain.qss`.
- Add or update small SVG action icons under `EGTRAIN/QEGTRAIN/resources/icons/` and register them in `EGTRAIN/QEGTRAIN/app/resources.qrc`.
- Extend the visual smoke control assertions.

### Work

1. Divide the toolbar into Case, Playback, and View groups with subtle 1-pixel separators and 8-pixel spacing. Do not add group cards or background fills.
2. Put Open Case in Case. It launches the existing startup chooser, where bundled and recent scenes, a scene folder, and legacy import remain available.
3. Put Run, Pause, Stop, and speed in Playback. Keep Run, Pause, and Stop adjacent.
4. Put Follow, the train selector, Zoom In, Zoom Out, and Fit in View.
5. Remove the toolbar case badge and toolbar clock. The left dock owns case identity; the timeline owns time.
6. Move Start Time into the Simulation menu and the scene-run setup path.
7. Keep the existing right-is-faster mapping, label the ends `Slower` and `Faster`, and add a regression check for the direction.
8. Keep Follow as a checkable action followed by the train selector. Give the selector a 140-pixel preferred width and a 200-pixel maximum.
9. Use icon-only Zoom In and Zoom Out controls. Keep Fit visible with the text `Fit` because it is the recovery action.
10. Replace the platform Run, Pause, and Stop media icons with one custom monochrome icon family. Set the toolbar icon size to 16 pixels and target a 32-pixel control height. Primary text buttons can reach 36 pixels; no toolbar control should create a taller row.
11. Use the native toolbar overflow menu only for secondary actions. Open Case, Run, Pause, Stop, and Fit must remain visible at 1024 by 720.

### Regression checks

- The visual smoke locates Open Case, Run, Pause, Stop, and Fit by object name at all three window sizes.
- It asserts that Stop follows Pause with no unrelated action between them.
- Keyboard traversal reaches Open Case, Run, Pause, Stop, speed, Follow, train selection, Zoom In, Zoom Out, and Fit in that order.
- Scene-render and legacy-import smoke hooks prove that both input paths remain reachable from Open Case.
- Run the full UI verification gate.

## Pull request 4: move the map key into the left rail and clarify layers

### Files

- Replace `EGTRAIN/QEGTRAIN/graphics/items/NetworkLegendItem.h` and `NetworkLegendItem.cpp` with `EGTRAIN/QEGTRAIN/widgets/NetworkLegendWidget.h` and `NetworkLegendWidget.cpp`.
- Modify `EGTRAIN/QEGTRAIN/app/MainWindow.h` and `MainWindow.cpp`.
- Modify `CMakeLists.txt` and `EGTRAIN/QEGTRAIN/resources/styles/egtrain.qss`.
- Add `EGTRAIN/QEGTRAIN/tests/test_networklegend.cpp`.

### Work

1. Remove the always-visible scene legend.
2. Add a collapsible `Map key` section below the layer controls in the Case and Layers dock. Keep each row within 160 pixels, start expanded on every launch, and keep the Map key header visible while collapsed. Do not persist this state with `QSettings`.
3. Add `Map key` to the View menu so a user can restore it.
4. Rename the primary toggles to `Stations and platforms`, `Trains`, `Signals`, and `Passengers`.
5. Add separate secondary toggles for `Station names` and `Train speed labels`. Do not hide an object just because its label is hidden.
6. Remove the custom solid-square indicator styling and use the native platform check mark. Keep a visible keyboard focus border.
7. Keep the case name once, followed by run-readiness text such as `Ready to run` or the first blocking validation error.
8. Include compact key entries for track states, train categories, station roles, signal cue shapes, and passenger-load indicators. Determine the entries when the case loads. Layer toggles must not make the key content jump or disappear.

### Regression checks

- `test_networklegend` checks every visible track, train, station, signal, and passenger entry against the same classification functions or constants used by the renderer.
- The visual smoke asserts that the map key is outside the `QGraphicsView`, can collapse, and can be restored from View.
- The map key starts expanded after every application restart and keeps the same entries while layers are toggled.
- Layer tests prove that object and label toggles act independently without changing scene ownership.
- Run the full UI verification gate.

## Pull request 5: revise the renderer palette and visual hierarchy

### Files

- Modify `EGTRAIN/QEGTRAIN/graphics/VisualPolish.h` and `VisualPolish.cpp`.
- Modify `TrackLineItem`, `SignalItem`, `TrainBadgeItem`, and the new station overlay item.
- Modify `EGTRAIN/QEGTRAIN/graphics/NetworkView.cpp` and `EGTRAIN/QEGTRAIN/resources/styles/egtrain.qss`.
- Update `docs/ui/renderer-style-map.md`.
- Extend `EGTRAIN/QEGTRAIN/tests/test_visualpolish.cpp`.

### Work

1. Apply the neutral shell and canvas colors from the audit, including the 1-pixel inset border that separates the surfaces.
2. Draw free track as a 2-pixel `#A0ACB4` solid line. Remove the undocumented 2, 3, and 4-pixel speed-tier encoding from the overview and delete `classifyTrackSpeed()` if no other consumer remains. Use Qt pen styles for state underlays: prepared is a medium dash-dot rail-blue line, occupied is a heavy solid red-orange line, and blocked is a heavy dashed amber line with the existing block marker.
3. Do not add custom chevrons, ties, animation, or track textures. Color, width, and Qt pen style provide the redundant cues.
4. Keep signal aspect colors. At overview, retain full emphasis for stop and caution cues while reducing repeated proceed signals to subdued ticks. Show full housings progressively as zoom increases. Their icon or silhouette must remain distinguishable in grayscale at minimum zoom.
5. Reduce train fill saturation. Preserve category shape differences and use text or icons for delay state.
6. Keep the followed or selected train identifiable on the canvas without relying on a tooltip. Use a wider selected badge or middle elision that preserves the distinguishing service suffix. Keep the full identifier in the selector and tooltip. Add a smoke assertion that two identifiers with the same prefix remain distinguishable.
7. Use the system fixed-width font for clock and timestep text. Keep the system UI font everywhere else.
8. Remove the adaptive background grid from the operational view because it has no distance or coordinate meaning.
9. Document exact color, width, dash, marker, and shape assignments in the renderer style map. Record the measured canvas contrast ratios: free 7.64:1, prepared 4.84:1, occupied 4.42:1, and blocked 7.61:1.

### Regression checks

- Unit tests assert the exact renderer mapping for every state.
- Add a grayscale image check that prepared, occupied, and blocked samples differ by stroke structure, not only luminance.
- Inspect the captures with common red-green deficiency simulation and record the result in the pull request.
- Run the full UI verification gate.

## Pull request 6: make startup state honest and add navigation coverage

### Files

- Modify the startup chooser and status handling in `EGTRAIN/QEGTRAIN/app/MainWindow.cpp`.
- Modify `tools/e2e/visual_polish_smoke.sh` and the in-app smoke hooks.
- Update `docs/guides/scenes-and-application.md` after the controls settle.

### Work

1. Replace `Skip` with a button that names the case already loaded from the command line, such as `Continue with Netherlands`. Derive the name from the active case instead of hardcoding it.
2. Closing or dismissing the chooser keeps the loaded case. The case label, readiness text, and status bar must agree about that state. Do not add an empty-state canvas over a prepared case.
3. Add a persistent `Network controls` entry under Help and to the network view context menu: `Drag to pan | Wheel to zoom | Ctrl+0 to fit | Right-click for actions`.
4. Remove the disabled `Planned route and related infrastructure` placeholder from the train context menu until that command works.
5. Add narrow and wide visual captures at 1024 by 720 and 1440 by 900.
6. Add a maximum-zoom capture and a fit-after-maximum-zoom assertion.
7. Record the 1200 by 800 captures at 1x and 2x device-pixel ratios.
8. Update the user guide with the final names and screenshots.

### Regression checks

- The startup smoke covers bundled case selection, continuing with the loaded command-line case, and Open Case. It proves that chooser dismissal does not produce false empty-state text.
- The loaded-case smoke proves that Network controls remains reachable from Help and the network context menu.
- The visual smoke proves that primary controls do not overflow and station labels remain in bounds at all supported sizes.
- Run `cmake --build build`, `ctest --test-dir build --output-on-failure`, and `tools/e2e/visual_polish_smoke.sh`.

## Completion gate

After all six pull requests:

1. Run the full build and CTest suite.
2. Run `tools/e2e/visual_polish_smoke.sh` and `tools/e2e/editor_smoke.sh`.
3. Run `tools/e2e/headless_smoke.py` to confirm the UI work did not change simulation output.
4. Compare default, dense, follow, narrow-window, and maximum-zoom states at all three window sizes. Compare the default state at 1x and 2x device-pixel ratios.
5. Run `graphify update .`.
6. Close the focused UI issues only after their acceptance checks pass. Close the student-workflow epic only when its remaining non-visual workflow issues are complete.
