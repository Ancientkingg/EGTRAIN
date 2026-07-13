# Track Preview Fallback Design

## Problem

Opening a V1 scene clears the existing network graphics and loads the editor model. Track geometry remains under `legacy/TrackLines`, while `infrastructure.json` contains placeholder `nodes` and `arcs` arrays that V1 does not read. The network view therefore stays black until `Run Scene` exports the scene, initializes the legacy simulator, and calls the full renderer.

An incomplete infrastructure-only case cannot reach that renderer. Scene validation blocks the run when services, train units, compositions, or routes are missing.

## Required behavior

- Opening a scene attempts a track-only preview immediately.
- The preview reads `legacy/TrackLines` or `legacy/Tracklines` without starting the simulator.
- Validation errors do not remove the preview. It remains visible as the fallback when `Run Scene` is blocked.
- A successful run replaces the preview with the existing interactive network rendering.
- Preview loading does not change or invent scene, train, route, signalling, or timetable data.
- Missing or malformed preview input does not block scene loading.

## Preview data

A small scene helper reads each numeric `B<number>` directory in numeric order. It parses `NodiCumPari.txt` rows as node ID, X coordinate, and Y coordinate. A track with fewer than two valid points is ignored.

When present, `Connections.txt` supplies links between tracklines. Each row identifies the first trackline and position followed by the second trackline and position. The preview interpolates those positions along the parsed polylines before drawing the link.

The helper returns plain preview data and diagnostics. It does not use the simulator's global `BlockSet`, `Node`, `Arc`, or signalling state.

## Rendering and fallback

`MainWindow::openSceneDirectory()` loads the scene, clears the previous network, refreshes the editor panels, and then asks the preview helper for geometry. The window draws each trackline and connection as a non-interactive light-gray path in `NetworkScene` and calls `fitView()`.

Raw X and Y coordinates determine the path. A small stable offset based on numeric trackline order separates coincident parallel lines. The preview does not draw trains, signals, blocks, station labels, or editable infrastructure objects.

The status bar reports the number of previewed tracklines and states that the simulation is not running. If no valid trackline is available, scene loading still succeeds and the status bar reports that no preview is available.

`MainWindow::runScene()` already checks validation before tearing down the current graphics. A blocked run therefore leaves the preview intact. A valid run reaches the existing teardown and `setupGUI()` path, which replaces the preview with the full network.

## Error handling

Unreadable directories, unreadable files, malformed rows, nonnumeric directory suffixes, and one-point tracks produce preview diagnostics but never scene validation errors. Valid tracks still render when sibling files are bad. The preview reports failure only when no track contains at least two valid points.

## Verification

One C++ regression test uses temporary directories to cover:

- both `TrackLines` spellings;
- numeric ordering such as `B2` before `B10`;
- valid node and connection parsing;
- malformed rows and one-point tracks;
- a missing preview directory.

The GUI smoke path opens an incomplete scene with valid legacy track data and asserts that the scene contains multiple graphics items with nonzero horizontal bounds. Existing scene-render and visual-polish smoke tests continue to cover the full renderer.

## Non-goals

- Running an incomplete simulation.
- Editing preview geometry.
- Persisting preview layout.
- Adding station labels, signals, blocks, trains, or timetable data to the preview.
- Defining canonical infrastructure fields in V1.

## Canonical infrastructure follow-up issues

The preview is a compatibility boundary, not the target infrastructure model. The work under issues #83 and #85 should be split into this dependency chain:

1. **Define canonical infrastructure data.** Specify fields and validation for tracks, nodes, arcs, switches, connections, gradients, speed limits, blocks, stations, and signalling references. Loaders and writers must preserve these fields instead of replacing `nodes` and `arcs` with empty arrays.
2. **Import legacy infrastructure into the canonical model.** Convert `TrackLines`, `Connections.txt`, station anchors, block data, and signalling topology. Add import, validation, and round-trip tests for all committed case studies.
3. **Render and simulate canonical infrastructure.** Build the network view and simulator topology from `SceneModel`. Remove the track-preview parser and the runtime dependency on `legacy/TrackLines` after the canonical path covers the committed cases.

The first issue defines the contract consumed by the next two. The compatibility preview stays isolated so the third issue can delete it without changing scene editing or simulation controls.
