# Using EGTRAIN and authoring V1 scenes

This guide covers the current V1 scene workflow. Use it to open and edit a scene, convert a legacy case, or prepare a new case for the simulator.

## Build the application

Run these commands from the repository root:

```bash
cmake -S . -B build -DEGTRAIN_BUILD_TESTS=ON
cmake --build build
```

On macOS with Homebrew Qt 5:

```bash
cmake -S . -B build -DEGTRAIN_BUILD_TESTS=ON \
  -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt@5
cmake --build build
```

Run the application from `EGTRAIN/QEGTRAIN`. Several legacy paths are relative to that directory.

```bash
cd EGTRAIN/QEGTRAIN
../../build/QEGTRAIN.app/Contents/MacOS/QEGTRAIN
```

The executable is `build/QEGTRAIN` on platforms where CMake does not create a macOS application bundle.

## Open and run a scene

1. Start EGTRAIN.
2. Choose `File > Open Scene...`.
3. Select the directory that contains `scene.json`.
4. Read the `Scene Validation` panel before editing.
5. Use `Save Scene As...` to make a working copy. Create the empty target directory before selecting it.
6. Edit compositions, services, timetable values, or incidents in the scene dock.
7. Choose `Run Scene`.

EGTRAIN validates the scene before each run. Errors block the run. Warnings remain visible but do not block it.

`Save Scene` writes the canonical JSON files. `Save Scene As...` also copies `legacy/` and `views.json`. A run uses the current model, including unsaved edits, and exports it to a temporary legacy input directory.

After setup, use the existing diagram windows to inspect speed, time, distance, delay, train path, and blocking time data.

Use `File > Load Legacy Case...` to convert a legacy folder into an editable V1 scene. Select the legacy source folder first, then a separate destination folder for the generated scene. The source is never modified. EGTRAIN shows importer, structural, and semantic diagnostics before opening the result; structural/import errors leave the current scene unchanged, while semantic errors remain visible in the validation panel so the result can be repaired.

## V1 scene directory

A V1 scene is a directory. Six JSON files are required.

| File | Required | Contents |
| --- | --- | --- |
| `scene.json` | yes | Name, schema version, base time, units, and optional source information |
| `infrastructure.json` | yes | Canonical nodes and arcs |
| `stations.json` | yes | Stations and platforms |
| `signalling.json` | yes | Signals, routes, and route block tokens |
| `rolling_stock.json` | yes | Train units, physical data, traction curves, and compositions |
| `services.json` | yes | Services, routes, stops, times, and repetition |
| `incidents.json` | no | Signal failures and train breakdowns |
| `views.json` | no | Display defaults |
| `legacy/` | needed by imported cases | Infrastructure and other data still read by the legacy simulator |

The current schema version is `1`. Supported units are metres, seconds, and metres per second.

The main references are:

```text
services.json
  composition -> rolling_stock.json
  route       -> signalling.json
  stop        -> stations.json
  platform    -> platform on that station

incidents.json
  target      -> signal or service
```

IDs must be unique within their collection. Keep station IDs free of whitespace because the legacy timetable format splits fields on whitespace.

Times in `services.json` are seconds relative to `base_time`. The first stop may omit its arrival. The last stop may omit its departure. A repeated service uses `repeat.headway_seconds`.

`loaded_data` is runtime metadata. EGTRAIN rebuilds it when the scene opens and does not write it to `scene.json`.

Use the [V1 scene property reference](v1-scene-properties.md) while editing JSON. The [scene schema reference](../architecture/scene-schema.md) lists the diagnostic codes and import and export behavior.

## Canonical files and legacy files

The canonical JSON files are the source of truth for an opened V1 scene. Do not edit an exported legacy directory and expect those changes to return to the scene.

The simulator still reads part of the old input structure. Imported scenes keep that data under `legacy/`. During a run, EGTRAIN writes canonical services, rolling stock, routes, and incidents to a temporary input directory, then copies passthrough files that do not replace generated files.

Do not delete files from `legacy/` until the scene exports and runs without them. Track infrastructure under `legacy/Tracklines/` or `legacy/TrackLines/` is required by the current simulator.

## Convert a legacy case

Use `scene_tool` when a case already exists in the legacy format, or use `File > Load Legacy Case...` for the same import path inside EGTRAIN. Import into a temporary directory first when checking a case from outside the repository.

```bash
./build/scene_tool import \
  EGTRAIN/QEGTRAIN/Input/Input_EGTRAIN_Netherlands \
  /tmp/netherlands-v1 \
  Netherlands

./build/scene_tool validate /tmp/netherlands-v1
```

The importer maps:

| Legacy source | V1 destination |
| --- | --- |
| `trainNames.txt` and `Trains/*.txt` | `services.json` |
| `TrainData/*.txt` | `rolling_stock.json` |
| `Routes/Route<N>.txt` | `signalling.json` |
| `Tracklines/Stations.txt` | `stations.json` |
| Remaining simulator data | `legacy/` |

Flat legacy payloads with root `Stations.txt`, `Connections.txt`, `TrackandStations.txt`, and numeric `B*` folders are accepted too; the importer places that infrastructure under `legacy/Tracklines/` without changing the source.

Inspect every import warning. `scene.import.adjusted` means the importer changed a value. The message names the affected file and service.

Export the result to another temporary directory:

```bash
./build/scene_tool export /tmp/netherlands-v1 /tmp/netherlands-export
```

Open `/tmp/netherlands-v1` in EGTRAIN and run it. Do not replace a committed scene until validation, export, and the simulation have passed.

## Start a scene without a legacy case

Use the minimal test fixture as the starting structure:

```bash
cp -R \
  EGTRAIN/QEGTRAIN/tests/fixtures/scenes/minimal \
  /tmp/my-scene
./build/scene_tool validate /tmp/my-scene
```

The fixture shows the required JSON shape. It does not contain enough track infrastructure for a real simulation.

For a runnable scene:

1. Set the name, schema version, base time, and units in `scene.json`.
2. Add stations and platforms.
3. Add train units and compositions.
4. Add routes with real block tokens.
5. Add services that reference those objects.
6. Add the required legacy track data.
7. Validate, export, and run one service.
8. Add the rest of the timetable after the first service completes its route.

Full infrastructure editing is not available in the V1 GUI. Build or import routes and track data outside the application, then use the GUI for compositions, services, timetable values, and incidents.

## Validation

Run validation after every small group of edits:

```bash
./build/scene_tool validate path/to/scene
```

Structural errors include missing files, invalid JSON, missing sections, and an unsupported schema version. EGTRAIN stops before semantic checks when structural loading fails.

Semantic errors include unresolved references, duplicate IDs, empty compositions or routes, invalid times, invalid dwell values, and bad incident targets. The [schema reference](../architecture/scene-schema.md#diagnostics) lists every code.

Useful checks for scene work are:

```bash
ctest --test-dir build -L scene-v1 --output-on-failure
ctest --test-dir build -L legacy-compat --output-on-failure
tools/e2e/headless_smoke.py
tools/e2e/roundtrip_smoke.py
```

Run the full set after changing committed scene data. For one scene, start with `scene_tool validate`, export it, and run it through the GUI.

## Netherlands case

The full legacy case is in `EGTRAIN/QEGTRAIN/Input/Input_EGTRAIN_Netherlands`. Its V1 counterpart is `EGTRAIN/QEGTRAIN/Scenes/Netherlands`.

The active manifest lists `Spr1.txt` through `Spr8.txt`. These services cover Utrecht (`Ut`), Hilversum (`Hvs`), Baarn (`Brn`), and Den Dolder (`Dld`). The committed train definitions use `SLT06.txt` with the matching `T_SLT06.txt` traction curve.

A fresh import contains:

- 41 stations
- 74 routes
- 5,429 route entries
- 8 services
- 1 train unit
- 1 composition

The import reports 30 timetable adjustments for intermediate departures stored as `-1`. These warnings are known. Validation and export still succeed.

The Amsterdam to Hilversum assignment is a separate piece of work. The legacy case contains candidate timetables for Sprinter and Intercity services, plus VIRM train data. Candidate route IDs are 29 through 36, 39 through 42, 53, and 54. Each route must be run and checked before it is assigned to a service.

Keep the full Netherlands scene as the reference case. Build the smaller assignment scene only after the selected Amsterdam to Hilversum services work in the full network.

See [Netherlands assignment brief](netherlands-assignment.md) for the work plan and checklists.

## Before committing a scene

- Validate the scene.
- Export it to a clean temporary directory.
- Run at least one train through every new route.
- Check station order and direction.
- Check planned arrival, departure, dwell, and entry times.
- Run the scene and legacy compatibility tests.
- Keep generated output, temporary exports, `.DS_Store`, and logs out of Git.
- Record source data and unresolved assumptions in the relevant guide.
