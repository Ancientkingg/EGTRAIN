# Build And Test

Run configure, build, and test commands from the repository root. Run the
application itself from `EGTRAIN/QEGTRAIN` so relative scene paths resolve.

## Requirements

- CMake 3.16 or newer
- C++17 compiler
- Qt 5 Core, Gui, Widgets, Charts, and Svg
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
brew install qt@5 zeromq cppzmq nlohmann-json
cmake -S . -B build -DEGTRAIN_BUILD_TESTS=ON -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt@5
```

On Ubuntu:

```bash
sudo apt-get install -y build-essential cmake \
  qtbase5-dev qttools5-dev qttools5-dev-tools libqt5charts5-dev libqt5svg5-dev libqt5network5 \
  libzmq3-dev cppzmq-dev nlohmann-json3-dev
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

### Build structure

The scene, simulation, utility and RailML code is built once as static
libraries: `egtrain_scene`, `egtrain_sim`, `egtrain_util`, `egtrain_railml`
and the vendored `egtrain_pugixml`. The Qt code that several targets use is
built once as well: `egtrain_graphics`, `egtrain_widgets`, `egtrain_update`,
`egtrain_telemetry`, `egtrain_dispatch` (`DispatchController` and
`SimulationWorker`) and `egtrain_diagrams`. `QEGTRAIN`, `scene_tool` and the
tests link them instead of listing their sources, so each file is compiled
once. A test gets only the library members it references. The application and
every test that pulls in Infrastructure, Signalling or RollingStock from
`egtrain_sim` define the global `Logger owl`.

Two things are still compiled more than once. The telemetry sources are built
again with test hooks as `egtrain_telemetry_hooks` when tests are built, and as
`egtrain_telemetry_smoke` for the application in the isolated telemetry smoke
configuration, because the hooks change the layout of the telemetry classes.
`TelemetryConsentDialog.cpp` is compiled in `QEGTRAIN` and in
`test_telemetryconsent` for the same reason. See
[Source layout](../architecture/source-layout.md#libraries).

### Warnings

The build passes `-Wall -Wextra` (`/W4` with MSVC) to the strict targets and
to `egtrain_dispatch`. The cache option `EGTRAIN_WARNINGS_AS_ERRORS` is OFF by
default. With `-DEGTRAIN_WARNINGS_AS_ERRORS=ON` a warning in a strict target
fails the build:

```bash
cmake -S . -B build -DEGTRAIN_BUILD_TESTS=ON -DEGTRAIN_WARNINGS_AS_ERRORS=ON
```

The pull request checks use the option on all three platforms. The targets are
listed in [Coding guidelines](coding-guidelines.md#warnings).

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

Tests can run side by side: `ctest --test-dir build -j N --output-on-failure`.
A test that starts the application keeps its user settings in a folder of its
own, most of them below `build/settings`, so none of them reads or changes the
settings of the user. CI runs `ctest --parallel` with the number of cores of
the runner.

Scene compatibility tests cover manifest probing, independent schema/bundle
classification, hostile newer bundles, and transactional test-only migration
chains. They also cover the validation of an upgraded copy, a source scene that
cannot be loaded, and a refused write. The refused-write case skips itself when
a new directory can still be created in a read-only one, for example for a
process with root rights. The production migration registry is empty on
purpose: every scene written so far has version number 1, so there is no
number to convert from.
`scene_tool` has no `migrate` command; it offers `import`, `pack`, `unpack`,
`export` and `validate`. The
[Compatibility boundary](../architecture/scene-model.md#compatibility-boundary)
says which scenes are supported.

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

`test_scenewriter` saves each committed scene and the `line` and `minimal`
fixtures, loads the result and saves it again, and requires the two saves to be
equal byte for byte. It never compares a save with the committed bytes, because
a committed file need not be in the writer's form.

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
| `test_csv_export_smoke`, `test_lebanon_scene_smoke`, `test_creator_acceptance_smoke`, `test_dialog_presentation_contract` | yes | yes | no | Bash scripts that use `awk`, `mktemp` and `/dev/stderr`; Windows has no bash on `PATH` that can be relied on. |
| `test_editor_smoke` | yes | yes | no | A Bash script; Windows has no bash on `PATH` that can be relied on. |
| `test_case_chooser_contract` | yes | yes | no | Compares backslash paths with the forward-slash paths the application reports, writes them into a `QSettings` INI file where backslash is an escape, and creates a directory symlink. |
| `test_package_contents_smoke` | yes | no | no | Checks the `.app` bundle layout with macOS tools. |
| `test_measure_peak_rss` | yes | yes | no | Tests the macOS `/usr/bin/time -l` collector. |
| `test_telemetrynetwork_reject`, `test_telemetrynetwork_trusted` | yes | yes | no | Qt 5.15 loads OpenSSL 1.1 (`libssl-1_1-x64.dll`, `libcrypto-1_1-x64.dll`) at run time. Windows does not provide it and Qt's installer no longer offers it, so Qt has no TLS there. |
| `test_windows_image_size` | no | no | yes | Reads the PE header of `QEGTRAIN.exe`. |
| `test_windows_default_stack` | no | no | yes | Reads the PE header of `scene_tool.exe`. |
| `test_win32_configure_rejected` | no | no | yes | Configures for 32-bit Windows with the Visual Studio generator and expects the message that only x64 is supported. |
| `test_startup_launch_contract` | yes | yes | partly | The two pseudo-terminal launches run only on macOS and Linux. |

The five Bash smokes (`test_csv_export_smoke`, `test_lebanon_scene_smoke`,
`test_creator_acceptance_smoke`, `test_editor_smoke` and
`test_dialog_presentation_contract`) read the application path from `QEGTRAIN_APP`.
`test_lebanon_scene_smoke`, `test_creator_acceptance_smoke` and
`test_dialog_presentation_contract` also read the `scene_tool` path from
`QEGTRAIN_SCENE_TOOL`. CTest sets both from the build targets. Run by hand
without them, the scripts use the macOS paths under `build/`.

`test_dialog_presentation_contract` runs `creator_acceptance_smoke.sh --dialog-contract`,
which starts the creator acceptance run and then checks the case chooser and the Run
simulation dialog in the built application, as [Dialog presentation](../ui/dialog-presentation.md#checks)
describes.

`test_headless_scene_smoke` and `test_pe_image_size` run on every platform.
`test_headless_scene_smoke` starts the built QEGTRAIN headless on Paimpol and
checks the exit code, the `End of Simulation` line and the energy output. It also
starts a run with `--detailed-trajectories` and checks that `TEMP/Traj_Train_*.txt`
and `TrainTrajectories/TrainPathDiagram.txt` appear only in that run.

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
program opens the scene, prepares a run, paints it and exits. Before the
launches it fails when a file of the package is an OpenMP runtime library or
names one (`vcomp140.dll` on Windows): the package ships none.

The assembly uses the same `windeployqt` options as the Windows package job
(`--no-opengl-sw`, `--no-angle`, `--no-system-d3d-compiler`,
`--no-virtualkeyboard` and `--no-quick-import` besides `--release`,
`--no-translations` and `--compiler-runtime`), and `test_ci_workflow` keeps the
two command lines equal. The Windows package keeps `vc_redist.x64.exe` (from
`--compiler-runtime`), the `bearer` plugin and the image format plugins.

### macOS package start

The macOS package job removes the virtual keyboard input plugin with the Quick
and QML frameworks that it links, and the WebP and TIFF image plugins with
their libraries, from the app after `macdeployqt` and before the ad hoc
signature. The CMake workflow assembles no macOS package. The package job runs
`tools/release/package_start_smoke.py` on a copy of the app (step **Start the
package**).

For a package with an app bundle, the script first reads the load commands of
every Mach-O file with `otool -l`. It fails unless every dependency is a system
library (under `/System/Library/` or `/usr/lib/`) or a file inside the app, and
every library inside the app is loaded by some file. The search starts at the
files in `Contents/MacOS` and `Contents/PlugIns`, which no load command names:
the system starts a program, and Qt finds a plugin by its directory. The script
then starts the app twice as for Windows: headless on Paimpol, and with a window
in startup timing mode. Only the window start loads the cocoa platform plugin of
the bundle.

The macOS package keeps `QtDBus` and `QtPrintSupport`, which the cocoa platform
plugin loads, the print support and bearer plugins, and the image format
plugins other than WebP and TIFF.

### Windows image size

Windows maps an EXE as one image whose size is `SizeOfImage` in the PE header,
and does not start an image near 2 GiB (it reports that the file is not a valid
Win32 application). `test_windows_image_size` fails when `SizeOfImage` of
`QEGTRAIN.exe` is above `EGTRAIN_MAX_PE_IMAGE_BYTES` (default 100000000), or
when the executable is not x64 and a Windows-subsystem program. A Release build
has an image of about 6 MB and a Debug build of about 18 MB, so the limit is
reached only when fixed-size static storage comes back. Set the variable at
configure time to change the limit. To check a build by hand:

```bash
python tools/release/pe_image_size.py build/Release/QEGTRAIN.exe --max-bytes 100000000
```

The first output line shows the measured size. The CI workflow prints the same
line in the job summary.

The same header holds the stack reserve. `QEGTRAIN.exe` is linked with 8 MiB,
the size the main thread has on macOS and Linux, and `test_windows_image_size`
checks that value. The other executables keep the linker default of 1 MiB, which
`test_windows_default_stack` checks on `scene_tool.exe`. On Windows the reserve
of an executable is also the stack size of every thread that is started without
one, so it is set for the application only. Measured on macOS: each of the six
committed scenes runs headless with the stack of the main thread limited to
128 KB, and none starts with 64 KB. To repeat it, run `ulimit -s 128` in a shell
and then the application with `--scene <scene folder> -g 0 -TSM 0 -RC 0`.

## Characterization tests

The characterization tests pin what the simulation core does today, so that
refactors of movement, signalling and global state show up as a reviewable
diff. They drive the real `DispatchController` in the test process on the small
scene `EGTRAIN/QEGTRAIN/tests/fixtures/scenes/line` ("Characterization Line"):
one track of 16 km in eight blocks of 2 km, three stations, seven services and
ten scenarios (`baseline`, `signal-failure-forward`, `signal-failure-reverse`,
`signal-failure-adjacent`, `signal-failure-staggered`, `signal-failure-last`,
`signal-failure-first`, `signal-failure-entered`, `signal-failure-late`,
`signal-failure-retarget`).
The stub cases add the routes `routeStubIn` and `routeStubOut` and the services
`U1` and `D1` to the scene in the test, because a route with `4-B0` or `5-B0` in
the fixture would add a movement authority to the cases with a signal failure on
those sections.
All railway and rolling-stock values are copied from the committed Assignment
scene. The signalling level is not part of the scene. The test sets it with one
network-wide signalling area, so one scene covers levels 0 to 5 and "none". The
border cases use two areas that meet at 8 km, the edge between `3-B0` and `4-B0`
at station B. In `border-0-2-near-fwd` and `border-0-1-near-fwd` they meet at
4 km, the edge between `1-B0` and `2-B0`.
[Signalling levels](../architecture/signalling-levels.md) says what each level
does and cites these cases.

A case is a scenario, a set of services and a level. The case table is in
`tests/characterization/test_characterization.cpp`:

| Cases | Run |
| --- | --- |
| `single-train` | train `T1` alone, no signalling area |
| `follow-level-none`, `follow-level-0` to `-5` | trains `F1` and `F2` following each other |
| `sf-forward-level-none`, `-0` to `-5` | the same trains with a signal failure from 400 s to 1000 s |
| `sf-reverse-level-none`, `-0` to `-5` | trains `R1` and `R2` in the opposite direction, same failure window |
| `sf-adjacent-level-none`, `-0` to `-2` | trains `F1` and `F2` with failures on the adjacent sections `4-B0` and `5-B0` from 400 s to 1000 s |
| `sf-staggered-level-none`, `-0` to `-2` | the same trains and sections, with the failure on `4-B0` ending at 700 s and the one on `5-B0` at 1000 s |
| `sf-last-level-none`, `-0` to `-2` | the same trains with a failure on `7-B0`, the last section of the route, from 400 s to 1000 s |
| `sf-first-level-none`, `-0` to `-2` | the same trains with a failure on `0-B0`, the first section of the route, from 0 s to 400 s; both trains wait to enter until the failure has ended, at every level; without a signalling level they enter together at 402 s, as nothing separates trains there |
| `sf-entered-level-none` | the same trains with a failure on `0-B0` from 90 s to 400 s, without a signalling level; `F1` has entered by then and runs on, `F2` waits to enter until the failure has ended |
| `same-entry-level-none`, `-0` to `-5` | trains `T1` and `F1`, both due at 60 s at the entry of the same route, without an incident; without a signalling level both enter at 60 s, as nothing separates trains there; at levels 0 to 5 `F1` enters after `T1` has moved on |
| `entry-order-level-0` to `-2` | trains `F2` and `L1` on the `sf-first` scenario; `F2` is listed before `L1` in the scene but `L1` is due first, so `L1` enters first when the failure ends |
| `sf-late-level-none`, `-0`, `-3` | train `F1` alone with a failure on `2-B0` from 188 s to 600 s; at 188 s `F1` is 316 m before the failed section at top speed, closer than its braking distance of 767 m, so it brakes with full force and its position never goes back; it passes the authority and runs through the failed section |
| `sf-retarget-level-0` | train `F1` alone with a failure on `6-B0` from 600 s and one on `5-B0` from 752 s to 1200 s; `F1` brakes to 11.1 m/s at the end of `4-B0` for the first failure, and 23 m before that point the second failure makes the end of `4-B0` a stop target, which `F1` cannot reach; it brakes with full force, its position never goes back, it passes the authority and stops at the end of `5-B0` for the failure on `6-B0` |
| `late-leader-level-3`, `-4` | trains `L1` and `F2`; `L1` is `F1` with a dwell of 100 s at C, so `F2` is held behind it there |
| `single-track-level-none`, `-0` to `-5` | train `S1` from A to B and `R1` from C to A, with a single-track restriction from `1-B0` to `4-B0`, protected by `0-B0` and `5-B0`; with a signalling level `R1` waits in front of the section while `S1` is in it |
| `single-track-follow-level-3`, `-4` | trains `F1` and `F2` in the same direction through the restricted section; the output equals `follow-level-3` and `-4`, because the restriction does not delay a train that follows the holder |
| `stub-departure-first-level-0` | trains `D1` (due at 60 s) and `U1` (due at 160 s) on a stub track, the closed end of the line from station B to station C, with a single-track restriction over `5-B0` to `7-B0`, protected by `4-B0` and `7-B0`; `D1` runs out of the stub and `U1` into it, and `U1` waits at the entry of its route until `D1` has left the stub |
| `stub-arrival-first-level-0` | the same stub with `U1` due at 60 s and `D1` due at 160 s; `D1` waits at the entry of its route until `U1` has left the stub |
| `border-0-2-fwd`, `border-0-2-rev` | level 0 from A to 8 km and level 2 from 8 km to C; `F1` and `F2` run from A to C, `R1` and `R2` from C to A, so a `rev` case enters on the C side and `border-0-2-rev` mirrors `border-2-0-fwd` |
| `border-2-0-fwd`, `border-2-0-rev` | the same trains with level 2 from A to 8 km and level 0 from 8 km to C |
| `border-0-3-fwd`, `border-0-3-rev` | the same trains with level 0 from A to 8 km and level 3 from 8 km to C |
| `border-0-1-fwd` | trains `F1` and `F2` with level 0 from A to 8 km and level 1 from 8 km to C |
| `border-0-2-near-fwd`, `border-0-1-near-fwd` | trains `F1` and `F2` with level 0 from A to 4 km and level 2 or level 1 from 4 km to C, so that the head of a train in `2-B0` has the level 0 sections `1-B0` and `0-B0` behind it |

All 75 cases run in CTest. They are listed in
`tests/characterization/CMakeLists.txt`, in the order of the table, and each
has a golden file. Any case can also be run by hand with `--case`. Each case is
its own CTest entry and process, labelled `characterization` and `unit`:

```bash
cmake --build build --target test_characterization test_crossover_chain test_signalling_aspects
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

These entries compare the observation of each run: what the test reads from
memory, and the two station statistics files. The other files of a run are not
read. `--repeat-files` runs the steps in the same way, but every run writes its
files into a folder of its own (`run-01`, `run-02` and so on, in a temporary
folder), and after the last run every distinct step runs once more in a fresh
process of the same executable, with the same working directory and
environment. Every file of every in-process run must then be equal, byte for
byte, to the file of the fresh run of its step, and both must hold the same set
of files. The files are the ones a headless run of the application writes with
`--detailed-trajectories`, including `TrainServicePathDiagram.txt`, which the
test writes after the run as the application does.
`TrainTrajectories/Computing_Times.txt` is the only file
whose content is not compared. It has to exist, but it holds wall-clock timings
and the computation time accumulated over the process, so it differs from run to
run. As with `--repeat`, a step has to appear twice. A difference is reported
with the run, the step, the file and the first differing line, and the folders
of a failed test are kept; the message names them. The fresh run is
`--single STEP --output-dir DIR`, which can also be used by hand. The folder
must not exist yet, because some files are written in append mode.

```bash
build/EGTRAIN/QEGTRAIN/tests/characterization/test_characterization \
    --repeat-files EGTRAIN/QEGTRAIN/Scenes/Paimpol EGTRAIN/QEGTRAIN/Scenes/Paimpol
```

Three entries run this way. `characterization_repeat_files_paimpol` (labelled
`slow`) and `characterization_repeat_files_lebanon_milano` run the sequences of
`characterization_repeat_paimpol` and `characterization_repeat_lebanon_milano`.
`characterization_repeat_files_fixture` runs `sf-forward-level-3` twice,
`single-track-level-4`, and `sf-forward-level-3` again on the line fixture,
which gives an incident and a single-track restriction that no committed scene
has. They fail when a run in a process writes a file that differs from the file
of a fresh process, which is what state left behind by an earlier run can cause
in a file that the entries above never read.

`test_crossover_chain` is a separate executable, because the harness above is
tied to the line fixture (its length, its block size and the station at 8 km).
It runs the scene `tests/fixtures/scenes/crossovers`: a chain of five tracks
with two crossovers that follow each other directly, so that the second half of
the first crossover and the first half of the second one share one long block.
The rolling stock and the timetable are copied from the line fixture. Platform
B lies inside the second crossover, and `F1` reaches it long before its planned
departure, so `F1` stands in the crossover while `F2` arrives. The geometry is
synthetic. For each of the levels 0, 1, 2 and 5, one CTest entry
(`characterization_crossover_chain_level_0`, `-1`, `-2`, `-5`) sets a
network-wide area of that level and runs train `F1` alone, train `R1` alone in
the opposite direction, and `F1` with `F2` behind it. A train alone has to reach
its last platform and stand only at platforms. The follower has to reach it too,
stay behind the rear of `F1`, and not enter the second crossover while `F1` is
in it. At levels 0, 1 and 2 it also has to wait in the second half of the first
crossover meanwhile; at level 5 it waits further back, in front of the first
crossover, and that is not checked. The levels 3 and 4 are not run, because a
train alone does not stop in front of the second crossover there. The test reads
no golden file.

`test_signalling_aspects` is a third executable, because it needs no scene and
no Qt. It builds routes of up to seven sections by hand in the global route
list and calls the release and the activation of the mixed signalling
(`releaseMixedSignallingSystem` and `activateMixedSignallingSystem`, in the
order of a step) and the release of the last section
(`relLastSectionMixedSignalling`). After a step it writes the code, state, signal
speed limit, speed in braking and exit speed of every section: one line per
case, with the group, the parameters, a `|` and one token
`code:state:signal:braking:exit` per section. The header of the golden file
`tests/characterization/expected/signalling-aspects.txt` explains the fields.
The groups are uniform levels, two levels with a border, three or more levels,
sweeps of a moving train that carry the state from step to step, the release
functions, other signal speeds and a held single-track zone. The cases are in
the file, and the comparison is exact text, without a tolerance. The speed
limits of the sections are numbers of the test, not railway data. The test also checks, without the golden file, the aspects of one step for
every level, and that a step leaves the lists of occupied and connected sections
as they were. A change of the aspect routines that is not meant to change
behaviour has to leave the file as it is. The file has no `# case:` line and no
`# known-wrong:` line, because it is not a case of the table. It is recorded
with `EGTRAIN_UPDATE_EXPECTATIONS=1`, like the others. The command in
"Changing an expectation deliberately" that runs every characterization test
records it too; this command records it alone:

```bash
EGTRAIN_UPDATE_EXPECTATIONS=1 build/EGTRAIN/QEGTRAIN/tests/characterization/test_signalling_aspects \
    --expect EGTRAIN/QEGTRAIN/tests/characterization/expected/signalling-aspects.txt
```

The test does not call diverging switches, double switches, station boundaries,
signal failures or the authorities of levels 3 and 4. Its CTest entry is
`characterization_signalling_aspects`, labelled `characterization` and `unit`.

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
  significant digits. An early arrival counts there as a delay of 0 in the
  total and in `N_StopTrains`. The average and deviation cover the delayed
  trains only.
- `signed_stats`: the same rows of `TrainTrajectories/Pos&Neg_Stats_Stations.txt`,
  where early arrivals are negative delays.

Both files take the arrival of a stop from its timetable point, the arrival that
`TimetablePoints.txt`, the timetable results, the diagrams and the CSV export
report, so a `result` line and a station row show the same delay. A train that
did not reach a stop, and a stop without a planned arrival, have no delay and are
counted in neither file. A stop with no timetable point keeps the arrival
recorded during the run. A station row without such a train holds `-1` in the
average, deviation, maximum, cumulative and percentage columns and 0 in
`Total_Delay` and the train counts. The
`Final_Station` row is the same statistic for the last stop of every train. The
first station of the network is not printed and not in `TOTALS`.

Integers and strings must match exactly. Floats match within an absolute
tolerance of 1e-4 and a relative tolerance of 1e-9, and the statistics rows
within 1e-5 relative. A failure prints the first 20 differing lines with the
expected and the actual line, and writes the actual output to
`<case>.actual.txt` in the test's build directory.

Each run also checks facts that do not come from the golden file, so a
re-recorded golden cannot hide them: speed never above the 36.11 m/s limit, no
backward movement, acceleration and braking within what the rolling stock can do,
no faster run to station B than the top speed allows (in a stub case only for a
train that starts before B), planned dwell and departure times kept, stops only at a platform, a
block boundary or behind another train, no overlap of two trains, and in every
station row of `stats` and `signed_stats` a `Total_Delay` and an `N_StopTrains`
that equal the sum and the number of the arrival delays of its `result` lines
(only the late ones in the sum for `stats`). In a `single-track-*` case `S1` and
`R1` are never inside the restricted section at the same time. In a stub case
both trains reach their last stop and are never inside the stub together. A case
without a signalling area is not checked for overlap or for the single-track
section: a scene without a signalling level does not separate trains, and
validation warns about it (`scene.signalling.level.missing`,
`scene.single_track.no_effect`).
The `-level-none` goldens therefore show trains at one position.

The signal states that the simulation hands to the window are checked at every
step of every case against the section codes of all routes. There must be one
entry per section and direction. Its code is the most restrictive code of the
route copies of the section (0, 751, 75, 180, 270 in that order). Its level is
the signalling level of the section, or none. Its failed flag is set exactly
from the start to the end of a signal failure.

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

Behaviour that is known to be wrong can be pinned and marked. The golden file
still pins it, so a refactor cannot change it unnoticed. The golden header
carries `# known-wrong: #<issue> <reason>` and the case table carries the same
text. The test fails when the two differ and prints `KNOWN-WRONG #<issue>` on
every run. A marked case is exempt from the checks on stops, separation and
timetable. The change that fixes the bug updates the golden file and removes the
marker in one commit. The markers are in the `kKnownWrong` table of
`test_characterization.cpp`. List the marked cases with:

```bash
grep -rn "known-wrong: #" EGTRAIN/QEGTRAIN/tests/characterization/expected
```

No case carries a marker. The table is empty, every golden header says
`# known-wrong: none`, the grep lists nothing and the checks apply to every case.

## Simulation Smoke Test

```bash
tools/e2e/headless_smoke.py
```

The smoke test runs Netherlands (`-n 1`), Paimpol (`-n 2`), Copenhagen
(`-n 3`), Brescia (`-n 4`), Assignment (`-n 5`), Lebanon (`-n 6`), and
Amsterdam_Hilversum_Student, which has no `-n` number and is case 7 of the
script. It checks clean native execution and the available trajectory/station
evidence. For every scene it also requires that no train moves farther in one
step than the highest maximum speed of the scene's rolling stock allows. The
script starts every run with `--detailed-trajectories`, because that check reads
`TEMP/Traj_Train_*.txt`.

For Amsterdam_Hilversum_Student the script also checks that every occurrence
of its service reaches Hilversum, one after the other, and that the five files
`infrastructure.json`, `rolling_stock.json`, `scenarios.json`, `stations.json`
and `views.json` equal the Netherlands files. Its `signalling.json` must equal
the Netherlands file apart from the signalling areas. CTest runs the file
comparison in `test_headless_smoke_decode`.

## Peak-memory measurement

On macOS, use the native peak-RSS collector for canonical Copenhagen and
Milano-Brescia Release runs. The protocol and generated record contract are in
[Peak-memory baselines](memory-baselines.md).

## Scene Roundtrip Smoke Test

```bash
tools/e2e/roundtrip_smoke.py
```

The roundtrip smoke validates, exports, reimports, and compares high-value
entity counts for all seven canonical scenes, then runs the small Assignment
reimport. It also checks that the exported `TrackLines/AreasCaseStudy.txt` has
one row per signalling area of the scene and that the reimport reports the file
in `import_report` without converting a row. Normal runs still load the
canonical source directory directly.

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
Follow station emphasis. Follow stays on after the followed train has left at the
displayed time and continues when the replay goes back; in a live run it is
switched off at that point. Unsuccessful and stopped runs have no replay, and a
stopped run keeps no results either.
Passenger journey details are unavailable in replay; snapshot scalar counts
and statuses remain visible.

The history (`app/GuiReplayHistory`) records every fifth simulated second and the
last one, in a compact form. What is the same for the whole run (identity and
static data of trains, the section and direction of each signal, section ids,
platforms) is stored once per run in a layout. Each frame stores only flat arrays
of the values that change: train positions, speeds and occupied arcs, signal codes,
levels and failure flags, section flags, platform queues, passenger states. Strings
that vary are stored once in a string table. `atOrBefore` rebuilds an ordinary
snapshot from this form, so the window uses the same snapshot type as in a live
run. It keeps the last rebuilt snapshot, and a snapshot that a caller still holds
is not rebuilt, so asking again for the same frame returns the same object. A
change of the static data between frames (a different number of trains, a renamed
train) starts a new layout; older frames keep theirs. Adding a field to a snapshot
type stops the build of `GuiReplayHistory.cpp` and of the round-trip test until
the field is stored.

The budget is 128 MiB of accounted payload and there is no frame count limit. When
a run does not fit, the oldest frames are dropped and the history reports the
interval it still covers (`firstTime`, `lastTime`, `evictedBeforeTime`). Accounting
includes the layouts, the string table and the capacities of the frame arrays,
not allocator bookkeeping, container nodes, shared-pointer control blocks or the
rebuilt snapshots that callers hold, so 128 MiB is **not** a precise
resident-memory limit; for the scenes below the heap in use was 1.1 times the
accounted bytes. If one frame, with its layout and strings, exceeds the budget,
replay is unavailable with an explanation; the simulation continues. A run without
a window (`-g 0`) builds no snapshots and no replay frames, because nothing reads
them.

Measured on a full run of each committed scene at the default horizon (`-g 1`,
`-pax 0`, Apple silicon, release build). "Complete snapshots" is what the same
frames would need as ordinary snapshot objects. The rows for Milano_Brescia,
Copenhagen and Netherlands come from runs without a signalling area; the
committed Milano_Brescia and Copenhagen scenes have one.

| Scene | Frames | Covers (s) | Accounted | As complete snapshots |
|---|---|---|---|---|
| Lebanon | 721 | 0-3599 | 0.6 MiB | 10.8 MiB |
| Assignment_Gvc_Gdg_Ut | 2001 | 0-9999 | 4.7 MiB | 32.5 MiB |
| Paimpol | 1801 | 0-8999 | 12.1 MiB | 155 MiB |
| Milano_Brescia | 801 | 0-3999 | 13.1 MiB | 68.8 MiB |
| Copenhagen | 1601 | 0-7999 | 78.9 MiB | 489 MiB |
| Netherlands | 1601 | 0-7999 | 32.7 MiB | 346 MiB |

Copenhagen is the largest: 196 trains, 78.9 MiB accounted for the whole run, 89 MiB
of heap in use, so a run of about 13000 simulated seconds fills the budget.
Recording a frame took 1 to 30 microseconds (Copenhagen and Netherlands: 21 to
30), once per five steps, on the simulation thread; rebuilding a frame took 1 to
34 microseconds on the interface thread.

## Close, New and Open during a run

Close, New Case Study, the Open commands and Load Legacy Case first ask a
running simulation to stop. The status bar shows "Stopping simulation..." and
the window stays usable. The command continues when the worker has finished,
and the current scene is replaced only then, so an open that fails leaves it in
place. `test_close_during_run_smoke` closes the window during an autostarted
run: the close request has to return with the run still stopping, and the
application has to exit by itself afterwards.

## Editor Smoke Test

```bash
tools/e2e/editor_smoke.sh
```

The script starts the application with the editor smoke hook and a settings
folder of its own. The first argument is an optional scene directory; the
default is `Assignment_Gvc_Gdg_Ut`, and `QEGTRAIN_E2E_SCENE_ALT` names the
second scene, `Copenhagen` by default. The application creates, edits,
validates, saves and reloads cases through the editors. It then checks their
layout: the main window does not grow beyond 1280 by 800 pixels when an editor
dock is shown at the normal and at 1.5 times the font size, a floated dock stays
within 750 by 600 and keeps its scroll area, and the Add button of the
Passengers editor is in view.

The smoke passes when the application exits with status 0, prints
`E2E_EDITOR_SMOKE_OK` and prints the marker of every facet, the last being
`E2E_EDITOR_LAYOUT_OK`. The application output goes to
`${TMPDIR:-/tmp}/qegtrain-editor-smoke-e2e.log`. On a failure the script prints
the end of that log, which holds `E2E_EDITOR_SMOKE_FAIL: <facet>: <message>`
when the application reported the failure.

CTest runs the script as `test_editor_smoke` on macOS and Linux with
`QT_QPA_PLATFORM=offscreen`. Run it alone with
`ctest --test-dir build -R test_editor_smoke --output-on-failure`. The script
takes the application from `QEGTRAIN_APP`. The release workflow also runs it as
a step of its own.

## GUI Smoke Test

```bash
tools/e2e/visual_polish_smoke.sh
```

Run this after UI or rendering changes.

The script also runs the signal head checks. `QEGTRAIN_E2E_SIGNAL_HEADS` names
what the run must show: `levels` (heads take stop, caution and proceed and
return to proceed), `failure` (the same, and failed heads exactly while a section
is blocked), `none` (every head unavailable) or `any`. Every rendered snapshot is
checked against the heads. `QEGTRAIN_E2E_PAUSE_STEPS` (for example `100,500,900`)
pauses the run at those steps and checks that the canvas shows the last delivered
snapshot; a run with pause steps waits 2 ms after every step, so that it cannot
end before a pause takes effect. After the run the final frame and every replay second, backwards and
forwards, are checked. The marker is `E2E_SIGNAL_HEADS_OK mode=<name>`. The script
makes three line scenes from `tests/fixtures/scenes/line` (levels, failure,
none) and also runs Paimpol, Assignment and Lebanon.

The explanation of Follow is checked by several smoke scripts. `app/FollowAvailability.h`
decides whether the control is offered, whether Follow can be switched on, whether it has
to be switched off, whether the view and the station emphasis follow the train, whether
the view glides to the train (a live run) or cuts to it (a replay) and which sentence is
shown; the window writes the answer into the train list, the status label and the
tooltips of the control. `graphics/FollowCamera` moves the view: a glide runs on a timer
of its own, and the window cuts to a train, stops the camera and settles it on its last
position. Each check reads that widget state, or the centre of the view, in the window's
own E2E code and prints a marker:

- `E2E_FOLLOW_NO_RUN_OK` (`scene_render_smoke.sh`, before the first run) and
  `E2E_FOLLOW_NO_SERVICES_OK` (`track_preview_smoke.sh`, a case without services):
  Follow is disabled, and its tooltip, the list entry and the label say why.
  `E2E_FOLLOW_SERVICE_ADDED_OK` (`creator_acceptance_smoke.sh`) checks that the
  first service changes that reason from "no trains" to "run the case first".
- In the live run of Copenhagen (`visual_polish_smoke.sh`):
  `E2E_FOLLOW_NOT_ENTERED_OK` (a train that has not entered shows its scheduled time in
  the label, the list entry, the tooltips and the accessible descriptions; such a train
  has no item in this run, so there is nothing for the view to move to, and
  `E2E_FOLLOW_REPLAY_BEFORE_OK`, which requires the item of the train to exist, checks
  that the view stays where it is for a train that has not entered),
  `E2E_FOLLOW_STATUS_WIDTH_OK` (a sentence does not raise the width of the status bar),
  `E2E_FOLLOW_VISIBLE_OK` (at a window width of 1024 pixels the label still shows the
  state of the train and its scheduled time, and the list asks its popup for a width
  that fits the entry, which is wider than the list; how a platform style opens the
  popup is not checked),
  `E2E_FOLLOW_CLOCK_OK` (a change of the start time changes the scheduled times in the
  list and in the sentence), `E2E_FOLLOW_ENTERED_OK` (the same train after it entered),
  `E2E_FOLLOW_LIST_STABLE_OK` (frames that change no state do not write the list: every
  row holds a marker text first, and the marker has to survive; Qt does not report a
  write of the text a row already has),
  `E2E_FOLLOW_LAYER_OK` (Trains layer off and on again, with the station emphasis) and
  `E2E_FOLLOW_LIVE_END_OK` (Follow is switched off with its sentence when the followed
  train leaves).
- The camera in the same live run. The checks give the camera a clock of their own: a
  frame is delivered while the clock stands still, and each tick advances it by one timer
  interval, so that they do not depend on wall time. `E2E_FOLLOW_CAMERA_GLIDE_OK` (a frame
  does not move the view, a tick moves it part of the way to the train, and without further
  frames, as in a paused run, the view settles on the train and the timer stops),
  `E2E_FOLLOW_CAMERA_RESUME_OK` (the first frame after a long pause does not move the view
  and starts a glide from where the view is), `E2E_FOLLOW_CAMERA_PAN_OK` (after a pan of
  the user and after a resize of the window, Follow stays on and the next frame starts a
  glide from where the view was left, which ends on the train),
  `E2E_FOLLOW_CAMERA_ZOOM_OK` (the zoom of the toolbar, in and out, centres the view on the
  train at once while the view lags behind it, and Fit stops the camera),
  `E2E_FOLLOW_CAMERA_CUT_OK` (choosing another train, and switching Follow on with the
  view panned away from the train, move the view to the train at once),
  `E2E_FOLLOW_CAMERA_STOP_OK` (switching Follow off, switching the Trains layer off, a
  train with no position on the map and the end of the followed train stop the timer and
  forget the target; with the layer on again and the position back, the view is on the
  train at once) and `E2E_FOLLOW_CAMERA_RUN_STOP_OK` (the Stop button ends a glide in
  progress). The check of the follow animation reads the centre of the view once the glide
  has ended, so it requires the view to end on the train.
- In the replay of the Assignment run (`visual_polish_smoke.sh`):
  `E2E_FOLLOW_REPLAY_BEFORE_OK`, `E2E_FOLLOW_REPLAY_DURING_OK` and
  `E2E_FOLLOW_REPLAY_AFTER_OK` (Follow stays on and the view moves only while the
  train runs; before the entry the train has a hidden item with the geometry of its
  run, away from the view, which the view must not move to; after the train has left,
  the camera holds no target; seeking within the run does not write the list),
  `E2E_FOLLOW_REPLAY_LAYER_OK` (Trains layer off and on
  again with Follow on), `E2E_FOLLOW_SELECT_ON_OK` and `E2E_FOLLOW_SELECT_OK` (the
  arrow keys of the list with Follow on and off) and `E2E_FOLLOW_RESET_OK` (Follow is
  off after the scenario changes). `E2E_FOLLOW_CAMERA_REPLAY_OK` (after the user has
  panned the view away, a seek and a step of the playback put the view on the train at
  once, with no glide). No train of that run leaves within its 600 s, so
  the frame after the end of the followed train is a copy of the frame in which it
  runs, with the train marked as left.

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

- `main` is the validation branch. Pull requests build the project and run
  the whole CTest suite, in parallel, on macOS, Windows (MSVC, Qt 5.15.2, vcpkg
  x64) and Linux (Ubuntu with apt Qt 5), unless every changed file matches the
  documentation filters. The same checks run on `main` once a night (02:17 UTC)
  and when started by hand from the Actions tab (`workflow_dispatch`); both
  ignore the filters. A push to `main` starts no run. A scheduled workflow runs
  on the default branch only, and GitHub pauses schedules in a repository
  without activity for 60 days. The three legs run independently
  (`fail-fast: false`) and share the same filters. A newer push to a pull
  request cancels its running checks; a scheduled or manual run is never
  cancelled. A failed leg uploads `ctest.log` and the GUI autostart log as an
  artifact named after the leg. Do not make any of these legs a required
  check; see the branch-protection note below.
- A pull request to `main` that changes a packaging input also runs the
  package check (`package-check.yml`). The inputs are the workflow files
  `package-check.yml`, `package.yml` and `release.yml`, `tools/release/`,
  `installer/`, `Info.plist.in`, `EGTRAIN/QEGTRAIN/update/` and
  `EGTRAIN/QEGTRAIN/app/main.cpp`. The check also runs once a night on `main`
  and can be started by hand, which covers changes that reach the packages
  through other files, such as the root `CMakeLists.txt`. The check selects the version as a release run that does not publish does (the CMake
  baseline, through `tools/release/version.py`) and calls the reusable workflow
  `package.yml`. A green check proves that the macOS, Windows and Linux packages
  build, pass the completeness checks of their jobs and are uploaded as
  artifacts of the run. The Windows package job also fails when the software
  OpenGL, ANGLE or Direct3D compiler libraries or Qt Quick, QML or virtual
  keyboard files are in the package, starts a copy of the package with
  `tools/release/package_start_smoke.py`, and prints the number of files and
  bytes of the package in its job summary. The macOS package job removes the
  virtual keyboard input plugin with the Quick and QML frameworks, and the WebP
  and TIFF plugins with their libraries, from the app before the signature,
  fails when one of them is in the app, starts a copy of the app with the same
  script, and prints the number of files and bytes of the app before and
  after the removal in its job summary. After the three packages are built,
  the job `release-assets` downloads the artifacts and runs
  `tools/release/build_release_assets.py` on them with the command line of the
  release job (`--artifacts artifacts --output release-assets`). The Windows
  package arrives as a directory with `QEGTRAIN.exe` at its top level. A green
  check therefore also proves that the script accepts the three real packages,
  builds the portable Windows archive and a manifest that the application
  accepts by its rules for the file list and the package sizes, and that the
  assets it writes are exactly the eleven that a release publishes.
  It does not prove a release: nothing is signed with real credentials (the
  macOS bundle carries the same ad-hoc signature as in a release), nothing is
  published, and the check has read permission only. It does not prove the
  release job either: that job runs only on a push to `production` or a `v*`
  tag, so its steps (the script step, the glob that publishes
  `release-assets/*`, the creation of the release) are first run by a release.
  The package check does not extract the archive on Windows; only the unit test
  of the script extracts an archive that it builds, on the Windows leg of the
  CMake workflow. A release still runs only from `release.yml`. A newer push to
  the pull request cancels the running check.
- `production` is the release branch. Its full pipeline packages macOS,
  Windows, and Linux applications, runs CTest, sanitizers, and the complete
  smoke suite, validates the scene bundles, and publishes a stable `vX.Y.Z`
  release. The three package jobs are in `.github/workflows/package.yml`, which
  `release.yml` calls from its `package` job with the selected version. The
  Windows package job uploads the assembled directory as the artifact
  `QEGTRAIN-windows-x64-payload`. The release job builds the files of the
  release from the downloaded artifacts with
  `tools/release/build_release_assets.py` and publishes the files it writes; the
  archive `QEGTRAIN-windows-x64.zip` is written only by the script. Before
  building, the pipeline increments the highest patch version
  among the CMake baseline, existing stable tags, and reserved release versions.
  All five build jobs, package metadata, and the update manifest use that same version. Local builds use the
  baseline unless configured with `-DEGTRAIN_VERSION=X.Y.Z`.
  `update-manifest.json` has one entry per distribution key under `assets`, each with
  `name`, `sha256` and `size`. The running copy picks its entry by key and downloads
  the asset that `name` gives. The keys are `windows-x64` (portable package), `windows-x64-installer`,
  `macos-arm64` and `linux-x86_64`; the running copy uses the portable package key
  on Windows and no installer key yet. The updater accepts a `name` only when it
  fits a pattern of its key, where `<version>` is the version of the manifest,
  which must equal the release tag without its leading `v`. The same patterns
  decide which release assets the update check keeps (`kAssetPatterns` in
  `update/ReleaseInfo.cpp`). The table lists the names a release may use; a name
  outside it needs a new application:

  | Key | Accepted names |
  | --- | --- |
  | `windows-x64` | `QEGTRAIN-windows-x64.zip`, `EGTRAIN-Portable-<version>-x64.zip` |
  | `windows-x64-installer` | `EGTRAIN-Setup-<version>-x64.exe` |
  | `macos-arm64` | `QEGTRAIN-macos-arm64.zip`, `EGTRAIN-<version>-macOS-arm64.dmg` |
  | `linux-x86_64` | `QEGTRAIN-linux-x86_64.AppImage` |

  A name with a path separator, another version or another kind of file is
  rejected. The manifest has no field for the kind of package; the key and the
  pattern decide it. The manifest of a published release keeps working because
  its names are in the table. Staging handles the archives and the AppImage only;
  the installer and the disk image are recognised but not staged yet.
  `update-manifest.json` also lists the files of the Windows package. Before
  the updater replaces an installation, it requires every listed file and a
  fixed set of runtime files (`requiredRuntimeFiles()` in
  `update/WindowsStaging.h`) in the extracted package. Keep that set in step
  with the package verification in the Windows package job.
- Production and tag releases run serially. A stale production run cannot
  publish after the branch advances, and an existing production release tag
  cannot be overwritten. If a failed-job retry encounters a used version,
  rerun all jobs on the latest production commit to allocate a new version.
- `v*` tags supply their application version explicitly. Tags containing a
  prerelease suffix remain prereleases and are not offered by the updater.
  `workflow_dispatch` validates the pipeline without publishing.
- Releases remain drafts until every package and scene bundle is uploaded.
  Stable production releases then appear in the application's update checks.

Automatic pull requests to `main` and pushes and pull requests to `production`
skip their workflows when changes are limited to Markdown files (`**.md`),
`docs/`, the root `LICENSE`, or `.github/ISSUE_TEMPLATE/`. Mixed changes still run the full
workflow, as do changes to source, tests, scenes, build settings, or workflows.
The package check is the exception on `main`: for a pull request it runs only
for the packaging inputs listed above.
Documentation-only production pushes do not publish a new release. Updated
packaged guides ship with the next release; `v*` tags and manual release runs
are not filtered by changed paths.

Do not require these path-filtered workflows, the package check included, as
branch-protection checks:
GitHub leaves skipped required workflows pending, which would block
documentation-only pull requests.

### Release assets script

`tools/release/build_release_assets.py` builds the files that a release
publishes from the artifacts of the package jobs:

```bash
python3 tools/release/build_release_assets.py --version X.Y.Z --artifacts DIR --output DIR
```

`--artifacts` holds one directory per artifact, as `actions/download-artifact`
makes them when it is given no artifact name:

- `QEGTRAIN-windows-x64-payload/`: the assembled Windows package, with
  `QEGTRAIN.exe` at its top level, uploaded by the Windows package job
- `QEGTRAIN-macos-arm64/QEGTRAIN-macos-arm64.zip`
- `QEGTRAIN-linux-x86_64/QEGTRAIN-linux-x86_64.AppImage`
- `EGTRAIN-scenes/`: the seven `.egscene` files

Other directories are ignored. `--output` must not exist or must be an empty
directory, and the script writes nowhere else. Afterwards it holds exactly the
eleven files of a release: `QEGTRAIN-macos-arm64.zip`,
`QEGTRAIN-windows-x64.zip`, `QEGTRAIN-linux-x86_64.AppImage`, the seven
`.egscene` files and `update-manifest.json`. The macOS package, the AppImage and
the scene bundles are copies. The Windows archive is written from the files of
the payload, one member per file with forward slashes, no directory entries and
no top-level folder. The `sha256` and `size` of each package in the manifest come
from the file in `--output`. The `files` of the Windows entry are the members of
the archive, sorted by their full path as a string (`a.b` comes before `a/b`).
The script prints the name and size of each file. The release job and the
package check run it with `--artifacts artifacts --output release-assets` on the
artifacts they download, and the release job publishes the files of the output
directory.

Every input is checked before `--output` is created:

- the version has the form `X.Y.Z` that `tools/release/version.py` accepts;
- each file above exists and is not empty, the three artifact directories hold
  no other file, and the payload holds regular files and directories only (a
  link is an error, and so is a directory that cannot be read);
- the file list of the Windows package has the rules of `parseManifestFiles` in
  `update/ReleaseInfo.cpp`: at most 4096 entries, each path at most 260 UTF-16
  code units, no `:` and no NUL, no empty, `.` or `..` segment, and
  `QEGTRAIN.exe` in the list. A backslash is also rejected, where the
  application would read it as a slash, because the list names the members of
  an archive that uses forward slashes only;
- each package is between 1 byte and 2 GiB, the limit of `parseUpdateManifest`
  in `update/ReleaseInfo.cpp`. The size of the Windows archive is checked once
  it is written, before the manifest is.

The script does not check the fixed runtime file set (`requiredRuntimeFiles()`
in `update/WindowsStaging.h`): the Windows package job verifies it before it
uploads the package.

The test is `tools/release/test_release_assets.py`. It builds a small artifact
tree, runs the script on it and checks the rules directly. Run it with
`ctest --test-dir build -R test_release_assets --output-on-failure`.

### Dependency caches

The Windows leg caches the Qt install (through `jurplel/install-qt-action`,
with the same cache entries as the Windows package job) and the vcpkg binary
archives in `runner.temp/vcpkg-binary-cache`. The vcpkg cache key is fixed,
`vcpkg-x64-windows-zeromq-cppzmq-nlohmann-json-v1`, and is saved only when the
restore missed. To invalidate it, for example after changing the vcpkg
package list, raise the `-v1` suffix by hand in the restore and save steps.
An existing key is never overwritten. Homebrew and apt packages are not
cached.

A pull request can restore caches written on `main`, not caches written by
other branches. The first run after a key change is therefore cold, and the
next nightly or manual run on `main` writes the caches that later pull requests
use. GitHub removes a cache that no run has used for seven days; the nightly run
keeps the entries in use.

## Verification Gates

To run what CI runs, use `ctest --test-dir build --output-on-failure` (add
`-C Release` with a multi-config generator, and `-j N` to run tests side by
side). For a quick check, use
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

For documentation changes, run `python3 tools/docs/check_docs.py`. It checks
relative links, heading anchors, image references and alt text, images below
`docs/` that no document uses, documents below `docs/` that `docs/README.md`
does not reach through links, the syntax of `bash`, `sh` and `json` examples
(a `bash` or `sh` example with a here-document is skipped), em dashes and en
dashes, and absolute local paths. It prints one line per finding as
`path:line: [kind] message` and exits with 1 when it finds any. It exits with 2
when it cannot run, for example outside a Git repository. A `json` example must
be valid JSON, so an excerpt with comments or ellipses takes another info word
such as `text`. The script does not fetch external links and does not render
Markdown.
