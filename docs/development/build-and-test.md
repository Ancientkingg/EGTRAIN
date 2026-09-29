# Build And Test

Run configure, build, and test commands from the repository root. Run the
application itself from `EGTRAIN/QEGTRAIN` so relative scene paths resolve.

## Requirements

- CMake 3.16 or newer
- C++17 compiler
- Qt 5 Core, Gui, Widgets, Charts, and Svg
- OpenMP runtime
- ZeroMQ, cppzmq, and nlohmann-json

`Qt5::Svg` is required by the application and must be available with the
other Qt 5 modules.

## Configure

```bash
cmake -S . -B build -DEGTRAIN_BUILD_TESTS=ON
```

On macOS with Homebrew Qt 5:

```bash
brew install qt@5 libomp zeromq cppzmq nlohmann-json
cmake -S . -B build -DEGTRAIN_BUILD_TESTS=ON -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt@5
```

## Build

```bash
cmake --build build
```

## Run a local build

From `EGTRAIN/QEGTRAIN`, use the executable produced by the selected generator:

```text
# macOS
../../build/QEGTRAIN.app/Contents/MacOS/QEGTRAIN

# Windows PowerShell, multi-config generator
..\..\build\Release\QEGTRAIN.exe

# Linux
../../build/QEGTRAIN
```

A single-config Windows build may place the executable at
`..\..\build\QEGTRAIN.exe`. These are local build paths; downloaded release
packages are documented in the root [README](../../README.md).

Runtime output defaults to
`<QStandardPaths::AppDataLocation>/Output/<scene>`. It does not always use a
repository `Output/` directory. Set `QEGTRAIN_OUTPUT_DIR` to override the base
directory; EGTRAIN then writes `<override>/Output/<scene>`.

## Unit Tests

```bash
ctest --test-dir build --output-on-failure
```

Current tests cover time formatting, speed formatting, trajectory accessors,
blocking-time diagram data, visual classification, scene validation, explicit
legacy import/export, scene writing, both native runtime builders, canonical
TrackPreview rendering, transparent scene bundle round-trips/security limits,
and smoke output decoding.
Scene compatibility tests cover manifest probing, independent schema/bundle
classification, hostile newer bundles, and transactional test-only migration
chains. The production migration registry is empty; `scene_tool migrate` is a
future extension rather than a second migration implementation.

The native builders and TrackPreview tests operate on an in-memory canonical
`SceneModel`; the builders perform no input-file reads. GUI and headless runs
both enter the same `DispatchController::prepareScene` path.

Scene tests use CTest labels:

```bash
ctest --test-dir build -L scene-v1 --output-on-failure
ctest --test-dir build -L legacy-compat --output-on-failure
ctest --test-dir build -LE legacy-compat --output-on-failure
```

`scene-v1` covers the canonical scene model. `legacy-compat` covers only the
explicit importer/exporter boundary; normal simulation is not in that label.
`scene-v2` covers `.egscene` container round-trips and hostile-archive checks.

Run the focused bundle test with:

```bash
ctest --test-dir build -L scene-v2 --output-on-failure
```

## Simulation Smoke Test

```bash
tools/e2e/headless_smoke.py
```

The smoke test runs Netherlands (`-n 1`), Paimpol (`-n 2`), Copenhagen
(`-n 3`), Brescia (`-n 4`), Assignment (`-n 5`), and Lebanon (`-n 6`). It
checks clean native execution and the available trajectory/station evidence.

## Peak-memory measurement

On macOS, use the native peak-RSS collector for canonical Copenhagen and
Milano-Brescia Release runs. The protocol and generated record contract are in
[Peak-memory baselines](memory-baselines.md).

## Scene Roundtrip Smoke Test

```bash
tools/e2e/roundtrip_smoke.py
```

The roundtrip smoke validates, exports, reimports, and compares high-value
entity counts for all six canonical scenes, then runs the small Assignment
reimport. Normal runs still load the canonical source directory directly.

## Completed-run replay

After a successful, unchanged full run, the Replay row appears under the network.
Start, End, Play/Pause, the slider, its arrow keys and mouse wheel seek only the
latest completed run. Play advances one requested simulation second per wall
second; each selection displays the last recorded frame at or before that time,
not interpolated positions. The label reports the selected frame time, five-second
sampling cadence and actual coverage, including earlier-frame eviction. A replay
seek explicitly restores historical operational overlays; normal completion does
not. A new run or scene/scenario edit clears replay and returns active replay to
the authoring preview. Train, station and signal inspections remain selected
across seeks; future or exited trains do not appear on layer toggles or drive
Follow station emphasis. Unsuccessful and stopped runs have no replay.
Passenger journey details are unavailable in replay; snapshot scalar counts
and statuses remain visible.

The producer retains at most 8192 shared immutable frames and 64 MiB of
accounted payload, evicting oldest frames to keep a recent window. Accounting
includes nested vector and string capacities, not allocator bookkeeping,
container nodes or shared-pointer control blocks, so 64 MiB is **not** a precise
resident-memory limit. If one frame exceeds the payload limit, replay is
unavailable with an explanation; the simulation continues.

## GUI Smoke Test

```bash
tools/e2e/visual_polish_smoke.sh
```

Run this after UI or rendering changes.

## Smoke artifacts

Smoke scripts write temporary diagnostics below `${TMPDIR:-/tmp}`. GitHub
Actions routes `TMPDIR` to `$RUNNER_TEMP` (`runner.temp`) for CI diagnostics.
The visual and render smoke artifacts include:

- `qegtrain-visual-polish-e2e.png` and `qegtrain-visual-polish-e2e.log`
- `qegtrain-scene-render-e2e.png` and `qegtrain-scene-render-e2e.log`
- `ctest.log`, `qegtrain-editor-smoke-e2e.log`, and
  `qegtrain-gui-autostart-smoke.log`

## CI and release branches

- `main` is the validation branch. Pushes and pull requests build the project
  and run CTest unless every changed file matches the documentation filters.
- `production` is the release branch. Its full pipeline packages macOS,
  Windows, and Linux applications, runs CTest, sanitizers, and the complete
  smoke suite, validates the scene bundles, and publishes a stable `vX.Y.Z`
  release. Before building, the pipeline increments the highest patch version
  among the CMake baseline, existing stable tags, and reserved release versions.
  All five build jobs, package metadata, and the update manifest use that same version. Local builds use the
  baseline unless configured with `-DEGTRAIN_VERSION=X.Y.Z`.
- Production and tag releases run serially. A stale production run cannot
  publish after the branch advances, and an existing production release tag
  cannot be overwritten. If a failed-job retry encounters a used version,
  rerun all jobs on the latest production commit to allocate a new version.
- `v*` tags supply their application version explicitly. Tags containing a
  prerelease suffix remain prereleases and are not offered by the updater.
  `workflow_dispatch` validates the pipeline without publishing.
- Releases remain drafts until every package and scene bundle is uploaded.
  Stable production releases then appear in the application's update checks.

Automatic pushes and pull requests to `main` and `production` skip their
workflows when changes are limited to Markdown files (`**.md`), `docs/`, the
root `LICENSE`, or `.github/ISSUE_TEMPLATE/`. Mixed changes still run the full
workflow, as do changes to source, tests, scenes, build settings, or workflows.
Documentation-only production pushes do not publish a new release. Updated
packaged guides ship with the next release; `v*` tags and manual release runs
are not filtered by changed paths.

Do not require these path-filtered workflows as branch-protection checks:
GitHub leaves skipped required workflows pending, which would block
documentation-only pull requests.

## Verification Gates

For UI changes:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
tools/e2e/visual_polish_smoke.sh
```

For simulation, scene model, data conversion, or memory changes:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
tools/e2e/headless_smoke.py
tools/e2e/roundtrip_smoke.py
```

For scene-format changes, also pack/unpack and validate/export the committed
scene directories with `build/scene_tool`. For UI or rendering changes, also run
`tools/e2e/visual_polish_smoke.sh`.
