# Build And Test

Run configure, build, and test commands from the repository root. Run the
application itself from `EGTRAIN/QEGTRAIN` so relative scene paths resolve.

## Requirements

- CMake 3.16 or newer
- C++17 compiler
- Qt 5 Core, Gui, Widgets, Charts, and Svg
- OpenMP runtime
- ZeroMQ, cppzmq, and nlohmann-json
- Python 3.9 or newer, found by `find_package(Python3)` at configure time;
  CTest runs its Python tests with that interpreter. On Windows, set
  `PYTHONUTF8=1` for local runs so scripts read program output as UTF-8.

`Qt5::Svg` is required by the application and must be available with the
other Qt 5 modules.

Supported toolchains, as used by CI:

- macOS: Homebrew Qt 5 and the packages in the configure example below.
- Windows 10 or 11, x64 only: MSVC (Visual Studio 2019 or newer), Qt 5.15
  `msvc2019_64` with QtCharts, and vcpkg `zeromq cppzmq nlohmann-json` for the
  `x64-windows` triplet. 32-bit Windows is not supported.
- Linux: Ubuntu 24.04 with the apt packages in the configure example below.

## Configure

```bash
cmake -S . -B build -DEGTRAIN_BUILD_TESTS=ON
```

On macOS with Homebrew Qt 5:

```bash
brew install qt@5 libomp zeromq cppzmq nlohmann-json
cmake -S . -B build -DEGTRAIN_BUILD_TESTS=ON -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt@5
```

On Ubuntu:

```bash
sudo apt-get install -y build-essential cmake \
  qtbase5-dev qttools5-dev qttools5-dev-tools libqt5charts5-dev libqt5svg5-dev libqt5network5 \
  libzmq3-dev cppzmq-dev nlohmann-json3-dev libomp-dev
cmake -S . -B build -DEGTRAIN_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release
```

On Windows, from a Visual Studio developer PowerShell, with the vcpkg
packages installed and the Qt 5.15 `msvc2019_64` directory as
`CMAKE_PREFIX_PATH`:

```powershell
vcpkg install zeromq cppzmq nlohmann-json --triplet x64-windows
cmake -S . -B build -DEGTRAIN_BUILD_TESTS=ON `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_INSTALLATION_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows `
  -DCMAKE_PREFIX_PATH="C:/Qt/5.15.2/msvc2019_64"
```

The Visual Studio generator is multi-config: choose the configuration when
building and testing, as in the next two sections.

## Build

```bash
cmake --build build
```

With a multi-config generator, add the configuration:
`cmake --build build --config Release`.

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

With a multi-config generator such as Visual Studio, build and test the same
configuration, for example `cmake --build build --config Release` and
`ctest --test-dir build -C Release`. `ctest -N` lists the tests registered on
the current platform.

Scene compatibility tests cover manifest probing, independent schema/bundle
classification, hostile newer bundles, and transactional test-only migration
chains. The production migration registry is empty; `scene_tool migrate` is a
future extension rather than a second migration implementation.

The native builders and TrackPreview tests operate on an in-memory canonical
`SceneModel`; the builders perform no input-file reads. GUI and headless runs
both enter the same `DispatchController::prepareScene` path.

### Test labels

Every test has exactly one of `unit` or `integration`. Configuration fails
when a test has neither or both.

- `unit`: runs in-process or is a static check. It starts no QEGTRAIN,
  `scene_tool`, update helper, or socket server.
- `integration`: starts or inspects a built program (`QEGTRAIN`, `scene_tool`,
  `egtrain_update_helper`) or talks to a loopback server.
- `gui`: needs a Qt platform plugin (the tests use `offscreen`) or launches
  QEGTRAIN in GUI mode. Combine it with either of the labels above.
- `slow`: takes more than 20 seconds on a development machine.

To add a test and register its labels, see [Coding guidelines](coding-guidelines.md#tests).

```bash
ctest --test-dir build -L unit --output-on-failure
ctest --test-dir build -L unit -LE gui --output-on-failure
ctest --test-dir build -L unit -LE slow --output-on-failure
ctest --test-dir build -L integration --output-on-failure
ctest --test-dir build -LE slow --output-on-failure
ctest --test-dir build -L gui --output-on-failure
```

On Windows add `-C Release` to each command.

Scene tests use CTest labels too:

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

### Platform coverage

CMake prints one `Windows: skipping ...` status line at configure time for each
group of tests it leaves out. `yes` below means the test is registered on that
platform.

| Test | macOS | Linux | Windows | Reason |
| --- | --- | --- | --- | --- |
| `test_csv_export_smoke`, `test_lebanon_scene_smoke`, `test_creator_acceptance_smoke` | yes | yes | no | Bash scripts that use `awk`, `mktemp` and `/dev/stderr`; Windows has no bash on `PATH` that can be relied on. |
| `test_case_chooser_contract` | yes | yes | no | Compares backslash paths with the forward-slash paths the application reports, writes them into a `QSettings` INI file where backslash is an escape, and creates a directory symlink. |
| `test_package_contents_smoke` | yes | no | no | Checks the `.app` bundle layout with macOS tools. |
| `test_measure_peak_rss` | yes | yes | no | Tests the macOS `/usr/bin/time -l` collector. |
| `test_windows_image_size` | no | no | yes | Reads the PE header of `QEGTRAIN.exe`. |
| `test_startup_launch_contract` | yes | yes | partly | The two pseudo-terminal launches run only on macOS and Linux. |

The three Bash smokes read the application and `scene_tool` paths from
`QEGTRAIN_APP` and `QEGTRAIN_SCENE_TOOL`; CTest sets both from the build
targets. Run by hand without them, they use the macOS paths under `build/`.

`test_headless_scene_smoke` and `test_pe_image_size` run on every platform.
`test_headless_scene_smoke` starts the built QEGTRAIN headless on Paimpol and
checks the exit code, the `End of Simulation` line and the energy output.

### Windows headless runs

QEGTRAIN is a GUI-subsystem program on Windows (`WIN32_EXECUTABLE`). With
`-g 0` it opens no window and reports only through redirected output handles
and its exit code (0 on success, 1 for bad arguments or scene errors). Started
from a terminal it prints nothing, and `& QEGTRAIN.exe` in PowerShell does not
wait for it. Start it from a program that creates the process and reads the
pipes, such as `tools/e2e/headless_scene_smoke.py`, or from PowerShell with
`Start-Process`; the two output files must differ:

```powershell
$dir = Resolve-Path .\build\Release
$p = Start-Process -Wait -PassThru -NoNewWindow -WorkingDirectory $dir `
  -FilePath "$dir\QEGTRAIN.exe" `
  -ArgumentList '--scene','Scenes/Paimpol','-h','300','-g','0','-pax','0','-TSM','0','-RC','0' `
  -RedirectStandardOutput "$PWD\out.txt" -RedirectStandardError "$PWD\err.txt"
$p.ExitCode
```

The working directory is the one that holds `QEGTRAIN.exe`, where the build
copies `Scenes/`. Set `QEGTRAIN_OUTPUT_DIR` to keep the output out of the
profile directory.

### Windows image size

Windows maps an EXE as one image whose size is `SizeOfImage` in the PE header,
and does not start an image near 2 GiB (it reports that the file is not a valid
Win32 application). `test_windows_image_size` fails when `SizeOfImage` of
`QEGTRAIN.exe` is above `EGTRAIN_MAX_PE_IMAGE_BYTES` (default 2040109465), or
when the executable is not x64 and a Windows-subsystem program. Set the
variable at configure time to change the limit. To check a build by hand:

```bash
python tools/release/pe_image_size.py build/Release/QEGTRAIN.exe --max-bytes 2040109465
```

The first output line shows the measured size. The CI workflow prints the same
line in the job summary.

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

Smoke scripts write temporary diagnostics below `${TMPDIR:-/tmp}`; on Windows
the temporary directory comes from `TEMP` and `TMP`. GitHub Actions routes
`TMPDIR` (macOS, Linux) or `TEMP` and `TMP` (Windows) to `$RUNNER_TEMP`
(`runner.temp`) for CI diagnostics.
The visual and render smoke artifacts include:

- `qegtrain-visual-polish-e2e.png` and `qegtrain-visual-polish-e2e.log`
- `qegtrain-scene-render-e2e.png` and `qegtrain-scene-render-e2e.log`
- `ctest.log`, `qegtrain-editor-smoke-e2e.log`, and
  `qegtrain-gui-autostart-smoke.log`

## CI and release branches

- `main` is the validation branch. Pushes and pull requests build the project
  and run the whole CTest suite on macOS, Windows (MSVC, Qt 5.15.2, vcpkg
  x64) and Linux (Ubuntu with apt Qt 5), unless every changed file matches the
  documentation filters. The three legs run independently (`fail-fast: false`)
  and share the same filters. A failed leg uploads `ctest.log` and the GUI
  autostart log as an artifact named after the leg. Do not make any of these
  legs a required check; see the branch-protection note below.
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

### Dependency caches

The Windows leg caches the Qt install (through `jurplel/install-qt-action`,
with the same cache entries as the release workflow) and the vcpkg binary
archives in `runner.temp/vcpkg-binary-cache`. The vcpkg cache key is fixed,
`vcpkg-x64-windows-zeromq-cppzmq-nlohmann-json-v1`, and is saved only when the
restore missed. To invalidate it, for example after changing the vcpkg
package list, raise the `-v1` suffix by hand in the restore and save steps.
An existing key is never overwritten. Homebrew and apt packages are not
cached.

A pull request can restore caches written on `main`, not caches written by
other branches. The first run after a key change is therefore cold, and the
push to `main` after the merge writes the caches that later pull requests use.

## Verification Gates

To run what CI runs, use `ctest --test-dir build --output-on-failure` (add
`-C Release` with a multi-config generator). For a quick check, use
`ctest --test-dir build -L unit -LE slow --output-on-failure`.

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
