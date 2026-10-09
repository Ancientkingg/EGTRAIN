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
- Linux: Ubuntu (the `ubuntu-latest` runner image) with the apt packages in
  the configure example below.

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

The repository has no Visual Studio solution or project file. To work in Visual
Studio, open the repository folder as a CMake project (File > Open > Folder)
and give its CMake settings the toolchain file, the triplet and the prefix path
of the command above. Choose an x64 configuration and `QEGTRAIN.exe` as the
startup item. A configure for 32-bit Windows stops with a message that names
x64 as the only supported platform.

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
| `test_telemetrynetwork_reject`, `test_telemetrynetwork_trusted` | yes | yes | no | Qt 5.15 loads OpenSSL 1.1 (`libssl-1_1-x64.dll`, `libcrypto-1_1-x64.dll`) at run time. Windows does not provide it and Qt's installer no longer offers it, so Qt has no TLS there. |
| `test_windows_image_size` | no | no | yes | Reads the PE header of `QEGTRAIN.exe`. |
| `test_win32_configure_rejected` | no | no | yes | Configures for 32-bit Windows with the Visual Studio generator and expects the message that only x64 is supported. |
| `test_startup_launch_contract` | yes | yes | partly | The two pseudo-terminal launches run only on macOS and Linux. |

The three Bash smokes read the application and `scene_tool` paths from
`QEGTRAIN_APP` and `QEGTRAIN_SCENE_TOOL`; CTest sets both from the build
targets. Run by hand without them, they use the macOS paths under `build/`.

`test_headless_scene_smoke` and `test_pe_image_size` run on every platform.
`test_headless_scene_smoke` starts the built QEGTRAIN headless on Paimpol and
checks the exit code, the `End of Simulation` line and the energy output.

### Windows GUI tests

The offscreen Qt platform has no fonts of its own on Windows. CTest sets
`QT_QPA_FONTDIR` to the system font directory for every test with the `gui`
label. Set it yourself when you start a GUI test or QEGTRAIN with
`QT_QPA_PLATFORM=offscreen` by hand.

Qt 5.15 crashes when a `QMessageBox` is shown on the offscreen platform on
Windows: `QMessageBox::showEvent` asks for a native window handle through an
interface that this platform does not have. `test_scene_drop` needs the
unsaved-changes prompt, so CTest runs it with `QT_QPA_PLATFORM=windows` there.
A scripted run that reaches any other message box on the offscreen platform
fails the same way on Windows.

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

### Windows package start

After the tests, the Windows CI leg assembles the files of the release package
in a temporary directory: `QEGTRAIN.exe`, the vcpkg DLLs, the Qt DLLs and
plugins that `windeployqt` adds, and the scenes. It then runs
`tools/release/package_start_smoke.py` on that directory. The script removes
the Qt and vcpkg variables from the environment and leaves only the Windows
directories on `PATH`, so a DLL or plugin that is missing from the package
fails the launch. It starts the packaged program twice: headless on Paimpol to
the end of a 120 s run, and with a window in startup timing mode, where the
program opens the scene, prepares a run, paints it and exits.

### Windows image size

Windows maps an EXE as one image whose size is `SizeOfImage` in the PE header,
and does not start an image near 2 GiB (it reports that the file is not a valid
Win32 application). `test_windows_image_size` fails when `SizeOfImage` of
`QEGTRAIN.exe` is above `EGTRAIN_MAX_PE_IMAGE_BYTES` (default 1600000000), or
when the executable is not x64 and a Windows-subsystem program. Set the
variable at configure time to change the limit. To check a build by hand:

```bash
python tools/release/pe_image_size.py build/Release/QEGTRAIN.exe --max-bytes 1600000000
```

The first output line shows the measured size. The CI workflow prints the same
line in the job summary.

## Characterization tests

The characterization tests pin what the simulation core does today, so that
refactors of movement, signalling and global state show up as a reviewable
diff. They drive the real `DispatchController` in the test process on the small
scene `EGTRAIN/QEGTRAIN/tests/fixtures/scenes/line` ("Characterization Line"):
one track of 16 km in eight blocks of 2 km, three stations, five services and
three scenarios (`baseline`, `signal-failure-forward`, `signal-failure-reverse`).
All railway and rolling-stock values are copied from the committed Assignment
scene. The signalling level is not part of the scene. The test sets it with one
network-wide signalling area, so one scene covers levels 0 to 5 and "none".

A case is a scenario, a set of services and a level. The case table is in
`tests/characterization/test_characterization.cpp`:

| Cases | Run |
| --- | --- |
| `single-train` | train `T1` alone, no signalling area |
| `follow-level-none`, `follow-level-0` to `-5` | trains `F1` and `F2` following each other |
| `sf-forward-level-none`, `-0` to `-5` | the same trains with a signal failure from 400 s to 1000 s |
| `sf-reverse-level-none`, `-0` to `-5` | trains `R1` and `R2` in the opposite direction, same failure window |

All 22 cases run in CTest. They are listed in
`tests/characterization/CMakeLists.txt`, in the order of the table, and each
has a golden file. Any case can also be run by hand with `--case`. Each case is
its own CTest entry and process, labelled `characterization` and `unit`:

```bash
cmake --build build --target test_characterization
ctest --test-dir build -L characterization --output-on-failure
```

`characterization_repeatable` runs several cases in one process and then the
first one again. The two runs of that case must be identical, with no
tolerance. It fails when state from an earlier run leaks into a later one.
The same executable can run any scene directories back to back:

```bash
build/EGTRAIN/QEGTRAIN/tests/characterization/test_characterization \
    --repeat EGTRAIN/QEGTRAIN/Scenes/Paimpol EGTRAIN/QEGTRAIN/Scenes/Assignment_Gvc_Gdg_Ut \
    EGTRAIN/QEGTRAIN/Scenes/Paimpol
```

A step is a scene directory, optionally followed by `#` and a case name. Without
a case name the scene runs as committed.

Three more entries run committed scenes this way: `characterization_repeat_paimpol`
(Paimpol twice, then Assignment, then Paimpol again),
`characterization_repeat_lebanon_milano` (Lebanon and Milano-Brescia, alternating)
and `characterization_repeat_netherlands` (Netherlands, Lebanon, Netherlands, labelled
`slow`). They fail when a run leaves state behind that changes a later run of the
same scene, which is what a change to the global runtime arrays can cause.

### Reading a golden file

Golden files are in `tests/characterization/expected/<case>.txt`. Lines starting
with `#` are comments. The header names the case and the run. Every other line is
one fact: some key words, then `name=value` fields.

- `train <id> sample`: time `t` (s), route position `x` (m) and speed `v` (m/s),
  every 60 s from the first to the last step the train is in the network.
- `train <id> stop`: a stretch of at least 5 s below 0.001 m/s, with its first and
  last step, position and `dwell` (last minus first step).
- `train <id> braking`: the last step at the speed before a stop that is
  followed by a continuous speed decrease down to that stop.
- `separation <lead>><follower>`: the smallest distance between the rear of the
  leading train and the head of the one behind it, and when. Negative means
  overlap.
- `aspect <route> <section>`: the signal aspect code of the section as `step:code`
  at every change, starting at step 0 (270 clear, 180, 75, 0 stop, 751 BACC).
- `result`, `run_result`, `network`: timetable and run results as the application
  reports them. `n/a` means the value is not available.
- `blocktime`: the blocking-time records of a train.
- `incident`, `boundary`, `authority`: the incident definition, and for the steps
  just before and after its start and end, how many signal-failure authorities
  exist and which sections are blocked.
- `stats`: the rows of `TrainTrajectories/Stats_Stations.txt`, which holds six
  significant digits.

Integers and strings must match exactly. Floats match within an absolute
tolerance of 1e-4 and a relative tolerance of 1e-9, and the statistics rows
within 1e-5 relative. A failure prints the first 20 differing lines with the
expected and the actual line, and writes the actual output to
`<case>.actual.txt` in the test's build directory.

Each run also checks facts that do not come from the golden file, so a
re-recorded golden cannot hide them: speed never above the 36.11 m/s limit, no
backward movement, acceleration and braking within what the rolling stock can do,
no faster run to station B than the top speed allows, planned dwell and
departure times kept, stops only at a platform, a block boundary or behind
another train, and no overlap of two trains.

### Changing an expectation deliberately

Run the case with the update switch, then review the diff before committing:

```bash
EGTRAIN_UPDATE_EXPECTATIONS=1 ctest --test-dir build -L characterization
git diff EGTRAIN/QEGTRAIN/tests/characterization/expected
```

To run one case directly, call the executable:

```bash
EGTRAIN_UPDATE_EXPECTATIONS=1 build/EGTRAIN/QEGTRAIN/tests/characterization/test_characterization \
    --fixture EGTRAIN/QEGTRAIN/tests/fixtures/scenes/line \
    --expect EGTRAIN/QEGTRAIN/tests/characterization/expected --case follow-level-3
```

The switch rewrites the golden file and passes. It is refused when the `CI`
environment variable is set, and it writes nothing when a check above fails.
Read the whole diff and check that every changed number is a consequence of the
change you made and still physically plausible. Commit the new golden file in
the same commit as the behaviour change and give the reason in the commit
message. Do not edit the scene fixture to make a case pass. Changing it changes
every golden file, so add a service or scenario instead.

### Known-wrong behaviour

Some current behaviour is wrong. The golden file still pins it, so a refactor
cannot change it unnoticed, and it is marked. The golden header carries
`# known-wrong: #<issue> <reason>` and the case table carries the same text. The
test fails when the two differ and prints `KNOWN-WRONG #<issue>` on every run.
A marked case is exempt from the checks on stops, separation and timetable. The
change that fixes the bug updates the golden file and removes the marker in one
commit. The markers are in the `kKnownWrong` table of
`test_characterization.cpp`. List the marked cases with:

```bash
grep -rn "known-wrong: #" EGTRAIN/QEGTRAIN/tests/characterization/expected
```

The markers name three open issues. #437 covers two trains at one position
(`follow-level-none`, `sf-forward-level-none`, `sf-reverse-level-none`) and
trains that stay stopped after a signal failure at levels 0 to 2. #499 covers
a following train that reports a departure from B before its planned
departure at levels 3 and 4. #498 covers trains that stand until the end of
the run at level 5 (`follow-level-5`, `sf-forward-level-5`,
`sf-reverse-level-5`). No check fails for these three, because every stop is
at a block boundary or a platform, and the marker ties them to the issue. The
unmarked cases show no known-wrong behaviour, so the checks apply.

## Simulation Smoke Test

```bash
tools/e2e/headless_smoke.py
```

The smoke test runs Netherlands (`-n 1`), Paimpol (`-n 2`), Copenhagen
(`-n 3`), Brescia (`-n 4`), Assignment (`-n 5`), and Lebanon (`-n 6`). It
checks clean native execution and the available trajectory/station evidence.
For every scene it also requires that no train moves farther in one step than
the highest maximum speed of the scene's rolling stock allows.

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
Follow station emphasis. Unsuccessful and stopped runs have no replay, and a
stopped run keeps no results either.
Passenger journey details are unavailable in replay; snapshot scalar counts
and statuses remain visible.

The producer retains at most 8192 shared immutable frames and 64 MiB of
accounted payload, evicting oldest frames to keep a recent window. Accounting
includes nested vector and string capacities, not allocator bookkeeping,
container nodes or shared-pointer control blocks, so 64 MiB is **not** a precise
resident-memory limit. If one frame exceeds the payload limit, replay is
unavailable with an explanation; the simulation continues.

## Close, New and Open during a run

Close, New Case Study, the Open commands and Load Legacy Case first ask a
running simulation to stop. The status bar shows "Stopping simulation..." and
the window stays usable. The command continues when the worker has finished,
and the current scene is replaced only then, so an open that fails leaves it in
place. `test_close_during_run_smoke` closes the window during an autostarted
run: the close request has to return with the run still stopping, and the
application has to exit by itself afterwards.

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
  `update-manifest.json` also lists the files of the Windows package. Before
  the updater replaces an installation, it requires every listed file and a
  fixed set of runtime files (`requiredRuntimeFiles()` in
  `update/WindowsStaging.h`) in the extracted package. Keep that set in step
  with the package verification in the Windows job.
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
