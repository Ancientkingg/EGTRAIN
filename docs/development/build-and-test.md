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

The scene, simulation, utility, RailML and live frame and server code is built once as static
libraries: `egtrain_scene`, `egtrain_sim`, `egtrain_util`, `egtrain_railml`,
`egtrain_live` and the vendored `egtrain_pugixml`. The Qt code that several targets use is
built once as well: `egtrain_graphics`, `egtrain_widgets`, `egtrain_update`,
`egtrain_sceneopen` (`SceneOpenRequest`), `egtrain_openrelay` (`OpenRequestRelay`), `egtrain_telemetry`, `egtrain_dispatch` (`DispatchController` and
`SimulationWorker`) and `egtrain_diagrams`. `QEGTRAIN`, `scene_tool` and the
tests link them instead of listing their sources, so each file is compiled
once. A test gets only the library members it references. The application and
every test that pulls in Infrastructure, Signalling or RollingStock from
`egtrain_sim` define the global `Logger owl`. `egtrain_recovery` holds
`RecoveryStore` and `RecoveryController`, and `test_recoverystore` and
`test_recoverycontroller` are the only targets that link it.

Three things are still compiled more than once. The telemetry sources are built
again with test hooks as `egtrain_telemetry_hooks` when tests are built, and as
`egtrain_telemetry_smoke` for the application in the isolated telemetry smoke
configuration, because the hooks change the layout of the telemetry classes.
`TelemetryConsentDialog.cpp` is compiled in `QEGTRAIN` and in
`test_telemetryconsent` for the same reason. `update/SelfUpdater.cpp` is compiled
in `QEGTRAIN` and in `test_selfupdater` with `EGTRAIN_UPDATE_TEST_HOOK`, which adds
`SelfUpdater::setTestHook`. See
[Source layout](../architecture/source-layout.md#libraries).

With `EGTRAIN_BUILD_TESTS=ON`, `QEGTRAIN` also compiles the sources in
`EGTRAIN/QEGTRAIN/tests/e2e`, the runner for the scripted GUI scenarios. At the
start of `main()` it redirects the user settings to the folder named by
`QEGTRAIN_E2E_SETTINGS_DIR`. After the first show of the window it starts a
scenario, with blocking dialogs switched off, when the variable of that scenario
is set. A build without tests, such as the three package builds, compiles none
of it: in such a build the first show of the window starts no scenario and
`QEGTRAIN_E2E_SETTINGS_DIR` redirects no settings. `MainWindowTestAccess` is the
friend class of `MainWindow` that the runner calls. A scenario is a member of
`MainWindowTestAccess` defined in its own file in that folder (the legacy import
smoke is `LegacyImport.cpp`, the scene drop smoke `SceneDrop.cpp`, the scene
render smoke `SceneRender.cpp`, the station overlay smoke `StationOverlay.cpp`, the
track preview smoke `TrackPreview.cpp`, the visual polish smoke `VisualPolish.cpp`, the
editor smoke `EditorSmoke.cpp`, the creator acceptance smoke `CreatorAcceptance.cpp`). The
visual polish smoke also starts from the frames of a run: `renderSnapshot` calls the
`frameRendered` hook of `MainWindowTestHooks` after every frame, and the hook is set only
when `QEGTRAIN_E2E_VISUAL_POLISH` is set. The editor smoke sets the `passengerSource` hook
to the folder that Import Passengers reads in place of its dialog, and clears it afterwards.
The creator acceptance smoke starts without a scene: when `QEGTRAIN_E2E_CREATOR_ACCEPTANCE`
is set, `main()` opens the window through `e2e::runCreatorAcceptanceMain`, the first show asks
no startup question (`suppressStartupPrompts`), and capacity analysis shows its scope dialog
(`askCapacityScope`), which the scenario answers.

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
`test_sceneversions` reads the scenes in `tests/fixtures/scene-versions`. They
are byte copies of scenes written by older builds, listed with their origin in
`manifest.json`, and a fixture is never edited or saved again. For each one the
test checks the class, the version numbers, loading, validation, the sizes of
the loaded model, and that a copy upgrade is refused and leaves the fixture
unchanged. The stored draft-layout scene is pinned as not runnable. A rejected
schema number is checked on a copy that the test makes. A guard fails when a
version constant is raised without a fixture of the previous number and either
a migration for it or a manifest entry that lists it as unsupported.
`scene_tool` has no `migrate` command; it offers `import`, `pack`, `unpack`,
`export` and `validate`. The
[Compatibility boundary](../architecture/scene-model.md#compatibility-boundary)
says which scenes are supported.

The native builders and TrackPreview tests operate on an in-memory canonical
`SceneModel`; the builders perform no input-file reads. GUI and headless runs
both enter the same `DispatchController::prepareScene` path.

`test_recoverystore` checks `RecoveryStore` in one process: the layout of an
instance folder, writing and replacing a copy, a failed write that keeps the
previous copy, the copies that an ended instance left behind, the choice of the
newest complete bundle, unusable and unknown records, pruning, the limit of 20
kept instances, clear and close, adopt and discard, and the comparison of a
source with its copy. A case that needs POSIX permissions skips itself on
Windows and when the process may write into a read-only folder anyway.
`test_recoverystore_process` (label `integration`) starts the test program again
as a child that writes a copy and holds its lock, and as a child that ends
without a clean close. The parent checks that a scan lists nothing for the
running child, that it lists the copy once the child was killed or has ended,
and that a new store can adopt that copy.

`test_recoverycontroller` runs `RecoveryController` with an event loop and an
interval of 10 ms, which it sets in `QEGTRAIN_RECOVERY_INTERVAL_MS`; the default
is 30 s. It checks the interval variable, that the edits of one interval give
one write of the newest scene, that later edits do not move the timer, that a
revision on disk is not written again, that edits during a write give one more
write, that a save, a replacement or a return to the clean state leaves no copy
when a write is running or waiting, that shutdown waits for the write and
removes the instance folder, that the destructor leaves the copy, that a failed
write or removal is reported once per run, and the scan, adopt and discard
calls, including an adoption that fails after the removal of the previous copy
was handed to the worker. The test holds the worker thread at two points of a
job with a hook that the constructor takes, so the cases for a write in flight
do not depend on how fast the write is. The checks that nothing happens wait a
fixed 100 to 150 ms, and the timer case uses an interval of 2 s. The removal
case changes the permissions of a folder and is skipped where that has no
effect, as for a superuser and on Windows.

`test_openrequestrelay` (label `integration`) checks `OpenRequestRelay`, which
hands a file from a second launch to the running window. The test program is
the running instance, and it starts itself again as the second process. It
checks that a path with spaces and non-ASCII characters arrives once and
unchanged and that the second process ends with 0, that a request for another
application folder is refused, that a name held by a running instance is not
taken over while a name left behind by a killed one is, that a request in two
parts is accepted and bytes that are no request, or more than a request holds,
are dropped, that only the newest postponed request comes back, and the rule for
the test variables. The relay also takes the `QFileOpenEvent` that macOS sends
when Finder opens a file with the program. The test sends such events to the
application: the path of a file with spaces and non-ASCII characters is reported
once and unchanged from the event loop, an event for a URL that is no local file
is left alone, of two events in one turn the newest remains, and a request that
nothing is connected to yet waits for a receiver. On macOS and Linux the socket
files lie in a folder directly below `/tmp`, because a socket path is limited to
about 100 characters.
`test_startup_launch_contract` adds the window: a window started with a file,
and one with the chooser open, each open a file that a launch without test
variables hands to them, the window leaves a handed file that does not load out
of the recent cases, and a scripted launch with a file keeps its own window. A
run with any `QEGTRAIN_E2E_` variable neither listens nor hands over a file,
unless `QEGTRAIN_E2E_OPEN_RELAY` is set as well, which the contract sets for the
window only. The test hook `QEGTRAIN_E2E_OPEN_REQUEST` names a file that the run
hands to the application as a `QFileOpenEvent`, as the first show of the window
begins (`QEGTRAIN_E2E_OPEN_REQUEST_AT=launch`, the default), 50 ms later while the
program still starts (`startup`), from inside the open case chooser (`chooser`) or
one second later (`running`). It prints whether the chooser is open when the event
comes, and the chooser and the status line three seconds after it. The contract
uses it to show that a window started without a file opens a file from an open
event, with spaces or non-ASCII characters in its name, without the chooser, that a
running window opens one too, that an event from inside the open chooser closes the
chooser and opens the file, and that a malformed file is reported and stays out of
the recent cases. On macOS the startup callback waits 300 ms before it opens
the chooser, so that the event of a launch from Finder is seen first; the `startup`
moment checks that no chooser opens then. A real double-click in Finder is not part
of it.

### Test labels

Every test has exactly one of `unit` or `integration`. Configuration fails
when a test has neither or both.

- `unit`: runs in-process or is a static check. It starts no QEGTRAIN,
  `scene_tool`, update helper, or socket server.
- `integration`: starts or inspects a built program (`QEGTRAIN`, `scene_tool`,
  `egtrain_update_helper`, or a test that starts its own executable as a child)
  or talks to a loopback server.
- `gui`: needs a Qt platform plugin (the tests use `offscreen`) or launches
  QEGTRAIN in GUI mode. Combine it with either of the labels above.
- `slow`: takes more than 20 seconds on a development machine.
- `network`: sends a request to a server on the internet. It is registered only
  with `-DEGTRAIN_NETWORK_TESTS=ON`, so no other test run needs the internet.
  Combine it with `integration`. See [HTTPS update check](#https-update-check).

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
`scene-v2` covers `.egscene` container round-trips, hostile-archive checks and
the released scene fixtures.

`test_scenewriter` saves each committed scene and the `line` and `minimal`
fixtures, loads the result and saves it again, and requires the two saves to be
equal byte for byte. It never compares a save with the committed bytes, because
a committed file need not be in the writer's form.

The scene tests `test_scenebundle`, `test_scenewriter`, `test_scenecompatibility`,
`test_sceneimporter` and `test_sceneexporter` also give the scene API folder and file
names with Japanese and Thai characters, which no Windows code page holds together.
`test_scenebundle` also points the temporary folder at such a name.

Run the focused bundle test with:

```bash
ctest --test-dir build -L scene-v2 --output-on-failure
```

### Platform coverage

CMake prints one `Windows: skipping ...` status line at configure time for each
group of tests it leaves out. `yes` below means the test is registered on that
platform, and `partly` that it is registered and only some of its checks run there.

| Test | macOS | Linux | Windows | Reason |
| --- | --- | --- | --- | --- |
| `test_csv_export_smoke`, `test_lebanon_scene_smoke`, `test_creator_acceptance_smoke`, `test_dialog_presentation_contract` | yes | yes | no | Bash scripts that use `awk`, `mktemp` and `/dev/stderr`; Windows has no bash on `PATH` that can be relied on. |
| `test_editor_smoke` | yes | yes | no | A Bash script; Windows has no bash on `PATH` that can be relied on. |
| `test_scene_render_smoke`, `test_track_preview_smoke`, `test_legacy_import_smoke`, `test_route_diagram_smoke` | yes | yes | no | The first three are Bash scripts, and Windows has no bash on `PATH` that can be relied on. `test_route_diagram_smoke` is registered with them under `if(UNIX)`, so Windows does not register it either. |
| `test_visual_polish_smoke` | yes | no | no | A Bash script, registered on macOS only. |
| `test_case_chooser_contract` | yes | yes | no | Compares backslash paths with the forward-slash paths the application reports, writes them into a `QSettings` INI file where backslash is an escape, and creates a directory symlink. |
| `test_package_contents_smoke` | yes | no | no | Checks the `.app` bundle layout with macOS tools, and the `.egscene` document type that its `Info.plist` declares. |
| `test_measure_peak_rss` | yes | yes | no | Tests the macOS `/usr/bin/time -l` collector. |
| `test_telemetrynetwork_reject`, `test_telemetrynetwork_trusted` | yes | yes | no | Qt 5.15 loads OpenSSL 1.1 (`libssl-1_1-x64.dll`, `libcrypto-1_1-x64.dll`) at run time. Windows does not provide it and Qt's installer no longer offers it, so Qt has no TLS there. The telemetry sender uses Qt's network manager, which the WinHTTP transport of the update check does not replace, so the tests stay off. |
| `test_windows_image_size` | no | no | yes | Reads the PE header of `QEGTRAIN.exe`. |
| `test_windows_default_stack` | no | no | yes | Reads the PE header of `scene_tool.exe`. |
| `test_win32_configure_rejected` | no | no | yes | Configures for 32-bit Windows with the Visual Studio generator and expects the message that only x64 is supported. |
| `test_verify_signatures` | no | no | yes | Signs copies of `scene_tool.exe` with a throwaway self-signed certificate through `New-SelfSignedCertificate` and `Set-AuthenticodeSignature`, and runs `tools/release/verify_signatures.ps1` on them. It is not registered when no PowerShell is found. |
| `test_startup_launch_contract` | yes | yes | partly | The two pseudo-terminal launches, the window launch with a bundle argument, the open events and the file launches while a window runs run only on macOS and Linux. A bundle with a non-ASCII file name is launched without a window on all three systems. |
| `test_updatepreparation` | partly | partly | partly | The checks that stage a disk image and a zip package run on macOS only and need `hdiutil`, `ditto`, `codesign` and `python3`. The AppImage check runs on Linux only. |
| `test_make_dmg` | yes | partly | partly | The image, mount and signature checks need `hdiutil`, `ditto` and `codesign`. Elsewhere the test checks the file name, the input rules, the message for a missing tool, the command lines and the handling of a failing or timed-out command. |
| `test_sign_macos` | yes | no | no | Signs a fixture bundle with `tools/release/sign_macos.sh`, ad hoc and with the identity `-`, and needs `cc`, `codesign` and `ditto`. It uses no certificate or keychain. |

The update requests do not need Qt's TLS. On Windows `createHttpTransport` returns a
WinHTTP transport, so `test_httptransport` and `test_selfupdater` exercise it there. They reach
their server on the loopback address over plain http only because the tests ask for it;
the transport of the application fetches https addresses only.

Every Bash smoke in the table except `test_package_contents_smoke` reads the
application path from `QEGTRAIN_APP`; `test_visual_polish_smoke` passes it on to
the scripts it runs. `test_lebanon_scene_smoke`, `test_creator_acceptance_smoke`
and `test_dialog_presentation_contract` also read the `scene_tool` path from
`QEGTRAIN_SCENE_TOOL`. CTest sets these variables from the build targets. Run by
hand without them, the scripts use the macOS paths under `build/`.
`test_route_diagram_smoke` reads no variable: CTest passes the application and
the directory of the test executables as the two arguments of
`route_diagram_smoke.py`, which uses the paths under `build/` when it gets none.

`test_scene_render_smoke`, `test_track_preview_smoke`, `test_legacy_import_smoke`,
`test_route_diagram_smoke` and `test_visual_polish_smoke` are not registered in a
build with `EGTRAIN_ENABLE_SANITIZERS=ON`: the time limits of their scripts are set
for an uninstrumented build.

`test_dialog_presentation_contract` runs `creator_acceptance_smoke.sh --dialog-contract`,
which starts the creator acceptance run and then checks the case chooser and the Run
simulation dialog in the built application, as [Dialog presentation](../ui/dialog-presentation.md#checks)
describes.

`test_headless_scene_smoke` and `test_pe_image_size` run on every platform.
`test_headless_scene_smoke` starts the built QEGTRAIN headless on Paimpol and
checks the exit code, the `End of Simulation` line and the energy output. It also
starts a run with `--detailed-trajectories` and checks that `TEMP/Traj_Train_*.txt`
and `TrainTrajectories/TrainPathDiagram.txt` appear only in that run.
The tests `test_scene_version_run_*` also run on every platform. Each starts the
built QEGTRAIN headless on one runnable scene of `tests/fixtures/scene-versions`
and makes the same checks in both runs.

### HTTPS update check

`test_updatetransport_https` (labels `integration` and `network`) makes one real GET of
`https://api.github.com/repos/Ancientkingg/EGTRAIN/releases?per_page=1` through
`createHttpTransport` and reads the answer with `parseLatestStableRelease`. On Windows
that is the WinHTTP transport, so the test shows that a machine without a Qt TLS library
can make the request of the update check. It is registered only when the configuration
asks for it:

```bash
cmake -S . -B build -DEGTRAIN_BUILD_TESTS=ON -DEGTRAIN_NETWORK_TESTS=ON
ctest --test-dir build -L network --output-on-failure
```

On Windows add `-C Release` to the `ctest` command. Its output starts with `PASS`, `SKIP`
or `FAIL`. An answer with an HTTP status has reached the server, so the TLS handshake and
the certificate check worked, and the test tells the cases apart by what came back:

| Result | Cause |
| --- | --- |
| `PASS`, exit code 0 | The release list arrived and was read. A list whose newest release is a prerelease also passes. |
| `SKIP`, exit code 77 | GitHub answered with HTTP 403 or 429 (its rate limit) or with 5xx (an outage). CTest shows the test as skipped. |
| `FAIL`, exit code 1 | No HTTP response arrived: the name lookup, the connection, the TLS handshake or the certificate check failed, or 45 s passed. Also any other HTTP status, whose text says that it is not a TLS failure, and a body that is not a release list. |

The anonymous GitHub API allows 60 requests an hour for one address, and a run makes one
request. The program reads no rate-limit header, so every 403 counts as a rate limit.

`test_updatetransport_https_cases` runs the same program against a server on `127.0.0.1`
that gives each kind of answer, and runs in every configuration without the internet.

The Windows leg of the CMake workflow configures with `-DEGTRAIN_NETWORK_TESTS=ON`, leaves the
network tests out of the Test step with `-LE network`, and runs them in the step
Check HTTPS (Windows). The Windows package job builds the program with the package and runs
it in the step Check HTTPS from the package. That step copies the program into the copy of the
package that the start check used and runs it with only the Windows directories on `PATH`, so
it loads the Qt files of the package, as a machine without a developer setup would.

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
the Qt, vcpkg and library path variables from the environment and leaves only
the Windows directories on `PATH`, so a DLL or plugin that is missing from the
package fails the launch. It starts the packaged program in this order:

1. Headless on Paimpol to the end of a 120 s run.
2. With a window in startup timing mode, where the program opens the scene,
   prepares a run, paints it and exits.
3. The CSV export.
4. The PNG export.

The two export launches run Paimpol with a window and a horizon of 8000 s,
with the autostart hook and one export hook of the application each:
`QEGTRAIN_E2E_EXPORT_DIR` for the CSV files and `QEGTRAIN_E2E_ROUTE_DIAGRAM`
for the PNG files. The hook writes into a new temporary directory and then
exits the application, so each export is a launch of its own. The script
requires the marker that the application prints (`E2E_CSV_EXPORT_OK`,
`E2E_ROUTE_DIAGRAM_OK`) and the files that the hook names: the CSV files
`trajectory.csv`, `timetable.csv` and `run_summary.csv`, each not empty, with a
header line and a data line, and the PNG files `timetable_graph.png`,
`train_path_graph.png` and `route_reference_chooser.png`, each with a PNG
signature. It does not check the values in the files or how the images look.

Before the launches the script fails when a file of the package is an OpenMP
runtime library or names one (`vcomp140.dll` on Windows): the package ships
none.

The assembly uses the same `windeployqt` options as the Windows package job
(`--no-opengl-sw`, `--no-angle`, `--no-system-d3d-compiler`,
`--no-virtualkeyboard` and `--no-quick-import` besides `--release`,
`--no-translations` and `--compiler-runtime`), and `test_ci_workflow` keeps the
two command lines equal. The Windows package keeps `vc_redist.x64.exe` (from
`--compiler-runtime`), the `bearer` plugin and the image format plugins.

### macOS package start

The macOS package job removes the virtual keyboard input plugin with the Quick
and QML frameworks that it links, and the WebP and TIFF image plugins with
their libraries, from the app after `macdeployqt` and before the signature.
The CMake workflow assembles no macOS package. The package job runs
`tools/release/package_start_smoke.py` on a copy of the app (step **Start the
package**).

For a package with an app bundle, the script first reads the load commands of
every Mach-O file with `otool -l`. It fails unless every dependency is a system
library (under `/System/Library/` or `/usr/lib/`) or a file inside the app, and
every library inside the app is loaded by some file. The search starts at the
files in `Contents/MacOS` and `Contents/PlugIns`, which no load command names:
the system starts a program, and Qt finds a plugin by its directory. The script
then starts the app as for Windows: the headless run, the window start, the CSV
export and the PNG export. Only the launches with a window load the cocoa
platform plugin of the bundle.

The macOS package keeps `QtDBus` and `QtPrintSupport`, which the cocoa platform
plugin loads, the print support and bearer plugins, and the image format
plugins other than WebP and TIFF.

### Linux package start

The Linux package job extracts the AppImage that it built into a directory under
the runner temporary directory, so the file that is uploaded stays as built. The
job installs `xvfb` and `xauth` with the build tools. In step **Start the
package** it lists the plugin files of the extracted image in its log and runs
`tools/release/package_start_smoke.py` on the directory with `xvfb-run`, which
gives the program a virtual X display. The script starts the directory through
its `AppRun`, as the AppImage runtime does after it has mounted the image, and
makes the same launches as for Windows: the headless run, the window start, the
CSV export and the PNG export.

Before the launches the script runs `ldd` on every ELF file below `usr/bin` and
`usr/plugins`. It fails unless each Qt 5 and ZeroMQ library that such a file
loads is a file of the extracted image, and it fails when `ldd` lists no such
library at all. The runner has the Qt 5 packages that the build needs, so an
image that lacks a Qt library would otherwise still start there with the
library of the runner. The plugins are read as well as the programs, because
the platform plugin loads Qt libraries that the program does not. The job sets
no platform plugin variable, and the script removes every Qt variable and
`LD_LIBRARY_PATH` from the environment of the launches, so the image starts with
the platform plugin that it holds.

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

The passenger cases add station `P`, services `Q1` to `Q3` and passengers in the
test. Transfer cases also add `Platform_4` on B: its numeric label reaches the
walking time calculation, while `B.platform.1` reads as 0. Only transfer cases
have this platform, because its node adds a second timetable point named B.
Their B stops have no planned arrival. The cases set `PAX_GUI` directly with
the window off; the [command line](../guides/command-line.md) reads `-pax` only
with `-g 1`.

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
| `stub-tie-level-0` to `-5` | the same stub with both trains due at 60 s; the forward route of `U1` reserves the free stub at 59 s, and `D1` waits outside until `U1` has left; both reach their last stop |
| `stub-zero-level-0` to `-5` | the same stub with both trains due at step zero; step zero initializes trajectories and reserves before the first movement; both finish without overlap |
| `running-tie-level-0` to `-5` | S1 runs from A to B and a copy of R1 runs from C to B on existing blocks 7 to 4; both are moving and requesting when the free 4000 to 12000 m stretch is first reserved, and both finish without opposing overlap |
| `running-forward-first-level-0` to `-5`, `running-reverse-first-level-0` to `-5` | the same approaches with one entry 3 s later and capped at the existing 40 km/h approach speed; both are moving at different speeds, giving each direction a turn at priority |
| `running-follower-level-0` to `-5` | F2 follows S1 to B while R1 waits; all finish without opposing zone overlap |
| `stub-breakdown-level-0`, `-4` | the forward owner is held at its origin by a breakdown from 60 to 120 s; its reservation survives, the opposing train waits, and both finish |
| `running-through-level-0` to `-5` | the original full-route F1 and R1 on the line without a passing loop; no opposing zone overlap, but both cannot finish because the waiting loser occupies the winner's exit track |
| `border-0-2-fwd`, `border-0-2-rev` | level 0 from A to 8 km and level 2 from 8 km to C; `F1` and `F2` run from A to C, `R1` and `R2` from C to A, so a `rev` case enters on the C side and `border-0-2-rev` mirrors `border-2-0-fwd` |
| `border-2-0-fwd`, `border-2-0-rev` | the same trains with level 2 from A to 8 km and level 0 from 8 km to C |
| `border-0-3-fwd`, `border-0-3-rev` | the same trains with level 0 from A to 8 km and level 3 from 8 km to C |
| `border-0-1-fwd` | trains `F1` and `F2` with level 0 from A to 8 km and level 1 from 8 km to C |
| `border-0-2-near-fwd`, `border-0-1-near-fwd` | trains `F1` and `F2` with level 0 from A to 4 km and level 2 or level 1 from 4 km to C, so that the head of a train in `2-B0` has the level 0 sections `1-B0` and `0-B0` behind it |
| `pax-board-gui0`, `pax-board-gui1`, `pax-full-gui0`, `pax-full-gui1` | `Q1` boards passengers at P, including ties and an entry during the stop; a later entry stays at P. Full cases cut ties with three seats at P and B; alighting at B frees seats before boarding |
| `pax-transfer-gui0`, `pax-transfer-gui1` | `Q1` connects to `Q2` on the same platform and to `Q3` on another label; a passenger also starts a second journey on `Q3` |

Every case runs in CTest. They are listed in
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
`--single STEP --output-dir DIR`, which can also be used by hand. Use a new or
empty folder, because a run replaces only the files it writes.

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
no golden file. Each level also runs `R1` on a route marked `reversed` with
its sections in reverse order, with the same expectations, and refuses `F1` on
a route marked `reversed` with its sections in forward order with
`scene.route.direction`.

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
limits of the sections are numbers of the test, not railway data. The test also
checks, without the golden file, the aspects of one step for every level, the
release of one connected section on two routes of different lengths, the level
mask of a route, and that a step leaves the lists of occupied and connected
sections as they were. A change of the aspect routines that is not
meant to change behaviour has to leave the file as it is. The file has no
`# case:` line and no `# known-wrong:` line, because it is not a case of the
table. It is recorded with `EGTRAIN_UPDATE_EXPECTATIONS=1`, like the others. The
command in "Changing an expectation deliberately" that runs every
characterization test records it too; this command records it alone:

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
- `capacity_pair <leader> <follower>` and `capacity_resource <section>`: what
  `analyzeCapacity` and `analyzeAreaCapacity` report for the two trains of a
  case, with all sections of their route as the area and a period of 3600 s,
  the default of the capacity dialog. Only the cases `follow`, `sf-forward`,
  `sf-reverse`, `sf-adjacent`, `sf-staggered`, `sf-last` and `sf-first` at the
  levels 0 and 2 have them, and only when both trains have complete blocking
  times; `F2` is still in `7-B0` when `sf-adjacent-level-0` ends, so that case
  has none. The pair line gives `min_headway` (s) with the sections that govern
  it, `cycle` (s) and `cycle_pct` (percent of the period) from the first train
  to the second after compression, whether the compressed trains overlap
  (`conflict_free`), and the sections where they touch (`critical`). Each
  resource line gives the number of trains and occupations of a section, the sum
  of the occupation times (`occupation`, s), their union (`union`, s),
  `occupation_pct`, and `governing=1` for the section with the largest sum. The
  values cover these two trains only, not the capacity of the line.
- `incident`, `boundary`, `authority`: the incident definition, and for the steps
  just before and after its start and end, how many signal-failure authorities
  exist and which sections are blocked.
- `stats`: the rows of `TrainTrajectories/Stats_Stations.txt`, which holds six
  significant digits. An early arrival counts there as a delay of 0 in the
  total and in `N_StopTrains`. The average and deviation cover the delayed
  trains only.
- `signed_stats`: the same rows of `TrainTrajectories/Pos&Neg_Stats_Stations.txt`,
  where early arrivals are negative delays.
- `pax <id> state`: changes in network membership, status, active journey and trip,
  waiting station and platform, awaited and boarded trains, and arrival station.
- `gui_platform <station> <platform>`: changes in the ordered passenger IDs sent
  to the window; `-` means empty.
- `gui_passenger <id>`: changes in status, waiting platform, next train and next
  destination sent to the window.
- `onboard <train>`: changes in onboard count and capacity sent to the window.
- `journey <passenger> <journey>` and `trip <passenger> <trip>`: final start and
  completion flags, actual times and arrival delay, plus walking and waiting
  times for journeys. Passenger times are absolute seconds of the day; trace
  `t` values are steps of the run.
- `file PassengerStatus.txt` and `file JourneyDelays.txt`: every line of the two
  passenger files, as whitespace separated tokens.

Passenger traces store the first snapshot and print only later changes, in step
order. Passenger windows have equal ends, so no golden depends on the seed.

Transfers to a platform with another label never complete: the passenger stays
on the arrival platform. Waiting time is accumulated for the first journey
only. With `PAX_GUI` off the persistent platform lists stay empty. The snapshot
part for passengers who just left never matches when the base time is at or
above the horizon. A station with platforms on two nodes of one route has one
timetable point per node; result rows and statistics for a stop at that station
read the first point. See [Delay analysis](../guides/delay-analysis.md#limits).

Both station statistics files take the arrival of a stop from its timetable
point, the arrival that `TimetablePoints.txt`, the timetable results, the
diagrams and the CSV export report, so a `result` line and a station row show the same delay. A train that
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
both trains reach their last stop and are never inside the stub together. The
terminating running approaches also must all reach their last stop, and every
running case checks opposing zone overlap at every shared step. A case
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
step than the highest maximum speed of the scene's rolling stock allows, and
that no train steps against the direction of its route by more than 0.1 m. The
direction comes from the flag in the train's row in
`TrainTrajectories/TrainServicePathDiagram.txt`. The script starts every run with
`--detailed-trajectories`, because those checks read `TEMP/Traj_Train_*.txt`.
For Lebanon it also requires that its train reaches every stop of the route: a
stop that is not reached has the arrival `-10000` in `TimetablePoints.txt`.
CTest runs the position checks and the stop check in `test_headless_smoke_decode`
on trajectory and timetable files that the test writes.

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

## Leak check

LeakSanitizer does not run on macOS with Apple silicon, so the leak check is a Linux job
of the CMake workflow, `leak check (Linux)`. It builds the application and
`test_characterization` in a Debug configuration with `-DEGTRAIN_ENABLE_SANITIZERS=ON`, starts
them with `ASAN_OPTIONS=detect_leaks=1`, and fails on any leak or double free. It has two parts:

- `tools/e2e/leak_check_smoke.py` runs every scene directory below `EGTRAIN/QEGTRAIN/Scenes`
  headless, once, with `-h 600 --detailed-trajectories`. A scene fails when its log holds a
  LeakSanitizer or AddressSanitizer report, when the process exits with a code other than 0, or
  when it prints no `End of Simulation`. All scenes run before the script fails, so one start lists
  every failing scene.
- CTest runs `characterization_repeatable`, `characterization_repeat_paimpol`,
  `characterization_repeat_lebanon_milano` and `characterization_repeat_netherlands`. Each builds
  and runs scenes one after the other in one process (see [Characterization tests](#characterization-tests)),
  which is the scene reload. They cover the line fixture, Paimpol, Assignment, Lebanon,
  Milano-Brescia and Netherlands. Copenhagen and Amsterdam_Hilversum_Student run headless once and are
  not reloaded.

The job runs only in the nightly and the manual run, because the sanitizer build and the
instrumented runs take longer than the pull request checks. To check a branch before a merge,
start the workflow by hand from the Actions tab, or with `gh workflow run cmake.yml --ref BRANCH`;
the other legs start with it. The artifact `leak-check-logs` holds one `SCENE.log` per scene and
`ctest-reload.log`, also when the job fails.

`tools/e2e/lsan_suppressions.txt` lists the known leaks. Add one rule for each, with a comment that
names the cause and the change that removes the leak, or the library that owns it. A leak of the
simulation is fixed, not listed.

On macOS, `leaks --atExit -- COMMAND` lists the leaks of one run of a normal build. It exits with
status 1 when it finds any.

## Scene Roundtrip Smoke Test

```bash
tools/e2e/roundtrip_smoke.py
```

The roundtrip smoke validates, exports, reimports, and compares high-value
entity counts for all seven canonical scenes, then runs the small Assignment
reimport. It checks the compatibility rows in `TrackLines/AreasCaseStudy.txt`
and requires the ordered signalling areas, with their exported track scopes,
to survive through `TrackLines/EGTRAINSignallingAreas.json`. The import report
must account for every area. The importer warns about historical area files
without this sidecar and does not convert their rows. Normal runs load the
canonical source directory directly.

## Completed-run replay

After a successful, unchanged full run, the Replay row appears under the network.
Start, End, Play/Pause, the slider, its arrow keys and mouse wheel seek only the
latest completed run. Play advances one requested simulation second per wall
second; each selection displays the last recorded frame at or before that time,
not interpolated positions. The label beside the slider names the selected frame
time and keeps the width of its longest text, so the slider does not change its width
while the selection moves. The text below the controls does not depend on the selected
time, so the row keeps its height while the user seeks. It reports the interval that is
kept and the five-second sampling cadence; the slider covers exactly that interval and
Start goes to its first time. When the run is larger than the replay memory and the
oldest frames were dropped, the text says from which time the replay starts, as a clock
time and in seconds, and that the earlier part was not kept because the run is larger
than the replay memory, and the tooltip of Start says that it goes to the first kept
time. When not even one frame fits, the label says "No replay", the text says why, the
slider is empty and the slider and Play are disabled. A replay
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
them. `app/ReplayCoverage.h` turns the interval the history still covers into the words of
the row for a whole run, for a run whose first part was dropped and for a run that cannot
be replayed, and into the tooltip of Start; the window reads the history, asks for the
texts and writes them into the two labels and the tooltip.

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
of heap in use. A frame holds all the trains of the run, so a longer horizon, which adds
trains, makes every frame larger: with a horizon of 20000 s (`-h 20000`) Copenhagen has
476 trains and about 98 KiB a frame, against 50 KiB at the default horizon. The budget then
holds the last 1340 frames, from 13305 s to 19999 s (134144845 of 134217728 bytes), and the
row shows the run whose first part was dropped.
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

Run this after UI or rendering changes. On macOS CTest runs the same script as
`test_visual_polish_smoke`. The other GUI smokes are CTest tests of their own,
`test_scene_render_smoke`, `test_track_preview_smoke`, `test_legacy_import_smoke`
and `test_route_diagram_smoke` (see [Platform coverage](#platform-coverage)), and
`ctest --test-dir build -R <name> --output-on-failure` starts one. CTest gives these
tests and `test_visual_polish_smoke` a settings folder below `build/settings` and an output
folder below `build/output`; `visual_polish_smoke.sh` replaces the settings folder with a
temporary one. The release workflow also runs `scene_render_smoke.sh`, `track_preview_smoke.sh` and
`visual_polish_smoke.sh` as steps of their own. The scripts need an application built with
`EGTRAIN_BUILD_TESTS=ON`, because only that build starts a scenario from its variable at the first
show of the window and redirects the settings.

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

The replay row is checked in the Assignment run of the script, in the same window code as
the other replay checks. `E2E_REPLAY_ROW_WHOLE_OK`: the slider covers 0 to the last time, the
last time is the last timestep of the run, the text below the controls says that the whole run
is kept and from where to where, Start has no tooltip and goes to 0 s, the label beside the
slider names the selected frame, and the width of the slider and the height of the row are the
same with the first, the middle and the last frame selected. `E2E_REPLAY_ROW_DROPPED_OK`: the
product has no setting for the budget, so the check builds a second history from the frames
of the run, with a budget that holds about half of them, and hands it to the code that
fills the row after a run. The slider covers the kept interval, the text gives the start as
a clock time and in seconds and says why the earlier part is missing, the tooltip of Start
says that it goes to the first kept time, Start, a time before the kept interval and the
end of the slider select the frames of that interval, the row keeps its size while the
selection moves, and a change of the start time moves the clock in the text and in the
tooltip. The history of the run is put back afterwards. `E2E_REPLAY_ROW_UNAVAILABLE_OK`: a
history that kept no frame, because one frame did not fit its budget, gives the label "No
replay", an empty slider, and a slider and Play that are disabled. The wording is covered by
`test_replaycoverage`: a whole run, a run whose first part was dropped, a run that cannot be
replayed, a first kept time of 0, a run of one frame and a clock that wraps over midnight.

A real scene is checked with `QEGTRAIN_E2E_REPLAY_WHOLE_RUN=1`. The window runs the scene
to its end at its own horizon, applies the checks of `E2E_REPLAY_ROW_WHOLE_OK` to the
history of that run and exits, printing `E2E_REPLAY_WHOLE_RUN_OK` with the number of
frames, the last time and the accounted bytes and budget. `visual_polish_smoke.sh` does so
for all six committed scenes, so a history that drops the first part of one of them, or
ends before the last timestep of the run, fails the script. No committed scene is larger
than the budget at its own horizon, which is why the dropped row is built from a smaller
budget in the Assignment run.

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

`tools/e2e/creator_acceptance_smoke.sh --preview-infrastructure` compares the items of
the preview before Run with those of the prepared run (sizes, head positions, pens, z
values and stacking order). It reads `QEGTRAIN_APP` and `QEGTRAIN_SCENE_TOOL` like the
other Bash smokes (the paragraph below the table "Platform coverage" gives the defaults)
and prints `creator preview infrastructure smoke passed`. CTest runs the script only
without the option, as `test_creator_acceptance_smoke`, which checks the items of the
preview but does not compare them with a run.

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

The failure artifact of the pull request check holds only `ctest.log` and the
GUI autostart log, so the output of a failed smoke is what its script prints into
`ctest.log`. `visual_polish_smoke.sh` prints a missing-marker line or nothing for
most of its checks, so run it locally to read its logs.

## CI and release branches

- `main` is the validation branch. Pull requests build the project and run
  the whole CTest suite, in parallel, on macOS, Windows (MSVC, Qt 5.15.2, vcpkg
  x64) and Linux (Ubuntu with apt Qt 5), unless every changed file matches the
  documentation filters. The same checks run on `main` once a night (02:17 UTC)
  and when started by hand from the Actions tab (`workflow_dispatch`); both
  ignore the filters, and both also start the [leak check](#leak-check) on Linux.
  A push to `main` starts no run. A scheduled workflow runs
  on the default branch only, and GitHub pauses schedules in a repository
  without activity for 60 days. The three legs run independently
  (`fail-fast: false`) and share the same filters. A newer push to a pull
  request cancels its running checks; a scheduled or manual run is never
  cancelled. After the tests, the Windows leg also makes the request of the
  [HTTPS update check](#https-update-check) in a step of its own. A failed leg
  uploads `ctest.log` and the GUI autostart log as an artifact named after the
  leg. Do not make any of these legs a required check; see the
  branch-protection note below.
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
  `tools/release/package_start_smoke.py`, makes the request of the
  [HTTPS update check](#https-update-check) from that copy, and prints the number of files and
  bytes of the package in its job summary. It also prints the signature state of
  `QEGTRAIN.exe`, `egtrain_update_helper.exe` and `scene_tool.exe` with
  `tools/release/verify_signatures.ps1 -Report`, which fails for no file. A push
  to `production` or a `v*` tag runs the job `sign-windows` when the repository
  variable `WINDOWS_SIGNING` is `signpath`; it signs those three files and
  replaces the uploaded directory with one that holds them (see
  [Code signing](code-signing.md#signing-in-ci)). Every other run skips it. The
  Windows finalize job (`finalize-windows`) then downloads that directory, compiles
  `installer/egtrain.iss` with Inno Setup (the copy on the runner image, or a
  pinned version from Chocolatey when the image has none; the script needs 6.3
  or newer) into `EGTRAIN-Setup-<version>-x64.exe`,
  runs `tools/release/installer_smoke.ps1` on it (see
  [Installer smoke](#installer-smoke)), writes `EGTRAIN-Portable-<version>-x64.zip`
  from the same directory with `build_release_assets.py --payload`, extracts that
  archive and starts the extracted program with the same start script, and
  uploads the installer and the archive as the artifacts
  `EGTRAIN-windows-x64-installer` and `EGTRAIN-windows-x64-portable`. The smoke
  needs an installer of a higher version for its upgrade check, so the job
  compiles a second one with the next patch version and uploads only the first.
  When the job fails, it uploads the logs of Setup and of the uninstaller as the
  artifact `finalize-windows-diagnostics`. When `sign-windows` succeeded, the job
  `sign-windows-installer` then sends the uploaded installer to the signing
  service, requires valid signatures on the signed installer and on the three
  program files of the payload, and only then replaces the installer artifact
  with the signed file. The installer smoke runs on the installer before it is
  signed. The macOS package job builds for
  macOS 26 (`-DCMAKE_OSX_DEPLOYMENT_TARGET=26.0`) and writes the minimum system
  version of the executable into the `LSMinimumSystemVersion` key of the property
  list, so the app needs macOS 26 or later. It copies `scene_tool` and the Lebanon
  guide into the app, to `Contents/MacOS/scene_tool` and
  `Contents/Resources/lebanon-case-study.md`, and removes the virtual keyboard
  input plugin with the Quick and QML frameworks, and the WebP and TIFF plugins
  with their libraries, from the app. All of this comes before the one
  signature, which `tools/release/sign_macos.sh` makes (see
  [macOS signing script](#macos-signing-script)). After the signature the job fails when one of the removed files is in
  the app, runs `scene_tool` on the Lebanon scene of the app and Paimpol without
  the GUI, starts a copy of the app with the same script, and prints the number
  of files and bytes of the app before and after the removal in its job summary.
  In a push to `production` or a `v*` tag it then notarizes and staples the app
  when the environment holds notarization credentials (see
  [macOS notarization script](#macos-notarization-script)). It builds
  `EGTRAIN-<version>-macOS-arm64.dmg` from the app with
  `tools/release/make_dmg.py --layout`, and no later step changes the app. With
  a Developer ID identity the image is signed, and with notarization credentials
  it is notarized and stapled. The
  job mounts the image read-only and detaches it on every path. It checks that
  the volume holds only `QEGTRAIN.app`, the link `Applications` and the hidden
  file `.DS_Store`, that this file equals `tools/release/dmg/DS_Store`, that the
  app in the image and a `ditto` copy of it pass
  `codesign --verify --deep --strict`, that the property list of the app in the
  image declares `.egscene` as its one document type
  (`CFBundleDocumentTypes`) and exports the type `com.egtrain.simulator.egscene`,
  which conforms to `public.data` only (`UTExportedTypeDeclarations`), that the
  copy has the version and the identifier of the build, its programs and the
  guide, and that the copy runs Paimpol without the GUI to `End of Simulation`.
  The image is the only file of the artifact `QEGTRAIN-macos-arm64`; no zip is
  made. The Linux package job extracts its AppImage and starts the extracted
  image with the same script on a virtual display, after the script has checked
  that every Qt 5 and ZeroMQ library of the programs and plugins comes from the
  image. The start in each of the three package jobs includes the CSV and PNG
  exports of Paimpol through the test hooks of the application. The check does
  not show how the SVG icons look, typing into text fields, the native file
  dialogs, how Finder presents the disk image, whether a double-click on a
  `.egscene` file in Finder opens EGTRAIN, or a start on a machine without Qt or
  a display server; the clean-install rehearsal in the
  [release testing checklist](release-testing-checklist.md) covers them. After
  the three packages are built, the job `release-assets` downloads the
  artifacts and runs `tools/release/build_release_assets.py` on them with the
  command line of the release job
  (`--artifacts artifacts --output release-assets`). The Windows package
  arrives as a directory with `QEGTRAIN.exe` at its top level. A green
  check therefore also proves that the script accepts the real packages, the
  installer and the portable archive, that the archive holds the files of the
  payload with the same bytes, that the script builds a manifest that the
  application accepts by its rules for the file list and the package sizes, and
  that the assets it writes are exactly the files that a release publishes.
  It does not prove a release: nothing is signed with real credentials (the
  macOS bundle is signed ad hoc, as in a release without a certificate), nothing is
  published, and the check has read permission only. It does not prove the
  release job either: that job runs only on a push to `production` or a `v*`
  tag, so its steps (the script step, the glob that publishes
  `release-assets/*`, the creation of the release) are first run by a release.
  The finalize job extracts the archive with Python, not with the PowerShell
  command that the application uses; only the unit test of the script extracts an
  archive with that command, on the Windows leg of the CMake workflow. The
  runner account is an administrator, so the check does not show that a
  standard user installs without an elevation request. It runs on a machine
  with the Visual C++ runtime, so it does not show that the package starts on a
  machine without it. It does not show how Windows presents the installer (for
  an unsigned download, the SmartScreen screen), the Start Menu entry or the
  entry in the list of installed apps. The smoke reads the registry keys of the
  `.egscene` file type but opens no file, so it does not show that a double-click
  on a `.egscene` file in Explorer opens EGTRAIN. The
  installer and portable rows of the
  [release testing checklist](release-testing-checklist.md#windows-installer-and-portable-archive)
  cover the rest on a Windows machine. A release still runs only from
  `release.yml`. A newer push to the pull request cancels the running check.
- `production` is the release branch. Its full pipeline packages macOS,
  Windows, and Linux applications, runs CTest, sanitizers, and the complete
  smoke suite, validates the scene bundles, and publishes a stable `vX.Y.Z`
  release. The three package jobs are in `.github/workflows/package.yml`, which
  `release.yml` calls from its `package` job with the selected version. The
  Windows package job uploads the assembled directory as the artifact
  `QEGTRAIN-windows-x64-payload`, and the Windows finalize job builds the
  installer and the portable archive from it and uploads them as
  `EGTRAIN-windows-x64-installer` and `EGTRAIN-windows-x64-portable`. The release
  job builds the files of the release from the downloaded artifacts with
  `tools/release/build_release_assets.py` and publishes the files it writes; the
  portable archive is written only by the script. Before
  building, the pipeline increments the highest patch version
  among the CMake baseline, existing stable tags, and reserved release versions.
  Every build job, the package metadata and the update manifest use that same version. Local builds use the
  baseline unless configured with `-DEGTRAIN_VERSION=X.Y.Z`.
  `update-manifest.json` has one entry per distribution key under `assets`, each with
  `name`, `sha256` and `size`. The running copy picks its entry by key and downloads
  the asset that `name` gives. The keys are `windows-x64` (portable package), `windows-x64-installer`,
  `macos-arm64` and `linux-x86_64`. A Windows copy with `installed-by-setup.ini`
  next to `QEGTRAIN.exe` is installer-managed (`currentDistribution` in
  `update/Distribution.cpp`) and takes `windows-x64-installer`; every other copy
  takes the key of its platform, which for a portable Windows copy is `windows-x64`.
  An installer-managed copy does not update itself by replacing files: it downloads
  the installer and the update helper runs it. The installer is the only intended
  source of that file, and the package payload and the portable archive must not
  contain it.
  The updater accepts a `name` only when it
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
  its names are in the table. Staging handles the archives, the macOS disk image
  and the AppImage. The installer is checked against the manifest and staged as
  it is, without extraction. A package
  whose name ends in `.dmg` is mounted read-only below the staging folder with
  `hdiutil attach -readonly -nobrowse -noautoopen`, `QEGTRAIN.app` is copied from
  the root of the volume with `ditto`, and the volume is detached on every path
  (by force on the last attempt). The zip package of earlier releases, which
  holds `QEGTRAIN-Lebanon/QEGTRAIN.app`, can still be staged.
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
publishes from the artifacts of the package jobs, and writes the portable
Windows archive for the Windows finalize job:

```bash
python3 tools/release/build_release_assets.py --version X.Y.Z --artifacts DIR --output DIR
python3 tools/release/build_release_assets.py --version X.Y.Z --payload DIR --output DIR
```

`--artifacts` holds one directory per artifact, as `actions/download-artifact`
makes them when it is given no artifact name:

- `QEGTRAIN-windows-x64-payload/`: the assembled Windows package, with
  `QEGTRAIN.exe` at its top level, uploaded by the Windows package job
- `EGTRAIN-windows-x64-portable/EGTRAIN-Portable-X.Y.Z-x64.zip`
- `EGTRAIN-windows-x64-installer/EGTRAIN-Setup-X.Y.Z-x64.exe`
- `QEGTRAIN-macos-arm64/EGTRAIN-X.Y.Z-macOS-arm64.dmg`
- `QEGTRAIN-linux-x86_64/QEGTRAIN-linux-x86_64.AppImage`
- `EGTRAIN-scenes/`: the `.egscene` files of the case studies

Other directories are ignored. `--output` must not exist or must be an empty
directory, and the script writes nowhere else. Afterwards it holds exactly the
files of a release: the packages named above, the `.egscene` files of the case
studies and `update-manifest.json`. The packages and the scene bundles are copies.
The `sha256` and `size` of each package in the manifest come from the file in
`--output`. The `files` of the `windows-x64` entry are the members of the
portable archive, sorted by their full path as a string (`a.b` comes before
`a/b`). The `windows-x64-installer` entry has no `files`. The script prints the
name and size of each file. The release job and the package check run it with
`--artifacts artifacts --output release-assets` on the artifacts they download,
and the release job publishes the files of the output directory.

`--payload` takes the assembled Windows package and writes
`EGTRAIN-Portable-X.Y.Z-x64.zip` into `--output`: one member per file with forward
slashes, in the order of the sorted file list, no directory entries and no
top-level folder. The Windows finalize job runs it on the directory that it also
gives to Inno Setup, so the installer and the archive hold the same files. The
`--artifacts` form copies the archive and checks that it has exactly the members
of the payload artifact, in that order, with the same bytes.

Every input is checked before `--output` is created:

- the version has the form `X.Y.Z` that `tools/release/version.py` accepts;
- each file above exists and is not empty, the artifact directories hold no other
  file, and the payload holds regular files and directories only (a link is an
  error, and so is a directory that cannot be read);
- the portable archive is a readable archive that holds the files of the payload
  and nothing else, with the same bytes;
- the file list of the Windows package has the rules of `parseManifestFiles` in
  `update/ReleaseInfo.cpp`: at most 4096 entries, each path at most 260 UTF-16
  code units, no `:` and no NUL, no empty, `.` or `..` segment, and
  `QEGTRAIN.exe` in the list. A backslash is also rejected, where the
  application would read it as a slash, because the list names the members of
  an archive that uses forward slashes only;
- each package is between 1 byte and 2 GiB, the limit of `parseUpdateManifest`
  in `update/ReleaseInfo.cpp`. With `--payload` the size of the archive is
  checked once it is written.

The script does not check the fixed runtime file set (`requiredRuntimeFiles()`
in `update/WindowsStaging.h`): the Windows package job verifies it before it
uploads the package.

The test is `tools/release/test_release_assets.py`. It builds a small artifact
tree, with the portable archive written by the script, runs the script on it and
checks the rules directly. Run it with
`ctest --test-dir build -R test_release_assets --output-on-failure`.

### Disk image script

`tools/release/make_dmg.py` builds the macOS disk image from a finished
`QEGTRAIN.app`:

```bash
python3 tools/release/make_dmg.py --version X.Y.Z --app PATH/QEGTRAIN.app --output DIR [--layout]
```

The image is `DIR/EGTRAIN-<version>-macOS-arm64.dmg`, one of the names in the
table of accepted names above. It is a compressed, read-only (UDZO) HFS+ image
with the volume name `EGTRAIN`. The volume holds `QEGTRAIN.app` and
`Applications`, a symbolic link to `/Applications` to drag the app onto, and
nothing else: no scene folder, no `.egscene` file, no `scene_tool` and no guide.
The script copies the bundle with `ditto`, as the package job does, and does not
change it afterwards, so a signed app keeps a valid signature. The script prints
the name and size of the image.

With `--layout` the volume also holds the hidden file `.DS_Store`, a copy of
`tools/release/dmg/DS_Store`. It tells Finder to show the volume as an icon view
of 600 by 360 points with icons of 128 points and without toolbar, sidebar, path
bar and status bar, with `QEGTRAIN.app` on the left and `Applications` on the right
at the same height. There is no background picture and no volume icon. Without
`--layout` Finder places the two icons itself. The file is written without
Finder by `tools/release/dmg/make_ds_store.py`, which needs the `ds_store`
package. The package is not a dependency of the repository or of CI, so run the
script in a throwaway environment and commit the result:

```bash
uv run --no-project --with ds_store python tools/release/dmg/make_ds_store.py tools/release/dmg/DS_Store
```

The sizes and positions are constants at the top of that script. How the window
looks is checked by opening the image on a Mac.

These checks run before the script creates anything:

- the version has the form `X.Y.Z` that `tools/release/version.py` accepts;
- `--app` is a directory named `QEGTRAIN.app` that holds `Contents/Info.plist`
  and the directory `Contents/MacOS`;
- `--output` is not a file, and the image does not exist in it yet. The script
  never overwrites a file;
- with `--layout`, `tools/release/dmg/DS_Store` is a file;
- `ditto` and `hdiutil` are on the path. Both are macOS tools, so the script
  runs only on macOS.

Then it creates `--output` if needed, stages the app, the link and, with
`--layout`, `.DS_Store` in a temporary folder that it removes on every exit, and
runs `hdiutil create` on the folder.
Each tool gets 120 seconds, and only its exit status decides whether it worked:
`hdiutil` prints deprecation notices on standard error and still exits with 0.
When `hdiutil` fails or times out, the script removes the partly written image
and exits with the command and its error text.

Two images made from the same bundle have the same file name, volume name and
contents. The script does not promise equal image bytes. It sets no background
picture, volume icon or licence text, and it does not sign, notarize or upload
anything. The macOS package job in `package.yml` calls it once the app is signed
and checked, with `--output build/dmg --layout`, and uploads the image as the
artifact `QEGTRAIN-macos-arm64`.

The test is `tools/release/test_make_dmg.py`. On macOS it signs a small fixture
bundle (a program that exits with 0, a data file and a symbolic link inside the
bundle), builds the image and mounts it read-only below a temporary folder. It
checks the file name, the format, the volume name, the entries at the top of the
volume, that no `.egscene` file or `Scenes` folder outside the app is on the
volume, and that the app in the image equals the fixture in its paths, file
bytes, link targets and permission bits. It also checks that
`codesign --verify --deep --strict` passes on the app in the image and on a
`ditto` copy taken from it, and that the copy runs. It builds a second image
with `--layout` and checks that its `.DS_Store` equals the committed file and
that the app in it is unchanged. On every platform it checks the file name
against `kAssetPatterns` in `update/ReleaseInfo.cpp`, the input rules, the
message for a missing tool, and the command lines, the link, the staged
`.DS_Store` and the handling of a failing or timed-out command, with stand-ins
for `ditto` and `hdiutil`, and that the committed layout file names the app and
the link. Run it with
`ctest --test-dir build -R test_make_dmg --output-on-failure`.

### macOS signing script

`tools/release/sign_macos.sh <app>` signs the app in the macOS package job
(step **Sign the app**). Without `MACOS_SIGNING_IDENTITY`, or with it empty, it
runs `codesign --force --deep --sign -`, an ad-hoc signature that carries no
publisher. With the variable set to a Developer ID Application identity from a
keychain on the search list, it signs each Mach-O file and each nested
`.framework`, `.app` and `.xpc` bundle on its own, with `--options runtime
--timestamp`: first those in `Contents/Frameworks`, the deepest first, then
those in `Contents/PlugIns`, then the programs in `Contents/MacOS`
(`egtrain_update_helper` and `scene_tool`), then the main executable, and
finally the bundle, without `--deep`. It stops before it signs anything when a
Mach-O file is somewhere else in the bundle, and it prints no credential.

In the workflow, a push to `production` or a `v*` tag runs the job in the
environment `macos-signing`. The step **Import the signing certificate** reads
the environment secrets `MACOS_CERTIFICATE` (a base64 encoded `.p12` file with
the private key) and `MACOS_CERTIFICATE_PASSWORD`, and the variable
`MACOS_SIGNING_IDENTITY` (the name of the identity, as `security find-identity`
prints it). It imports them into a keychain of its own, passes the identity to
the later steps, and the last step of the job deletes the keychain. Without a
certificate the step does nothing and the app keeps the ad-hoc signature. The
step **Verify the signature** prints the identifier, authority and team of the
app, and requires an ad-hoc signature without an identity and a Developer ID
Application authority, a team, the hardened runtime and a timestamp with one.
Neither the workflows nor the release scripts remove the quarantine attribute or
turn Gatekeeper off; `test_ci_workflow` checks it.

The test is `tools/release/test_sign_macos.sh`. It builds a fixture bundle with a
framework, a flat library, a plug-in, two programs next to the main one and a
data file, and runs the script with a `codesign` in front of the real one that
records its arguments and turns `--timestamp` into `--timestamp=none`, because a
secure timestamp needs Apple's server. It checks that the ad-hoc path makes one
`--deep` call and that the bundle verifies with
`codesign --verify --deep --strict`. It runs the identity path with the identity
`-`, so that the real tool signs without a certificate, and compares the calls
with the expected order and options, checks that the bundle verifies and that
every piece of code has the hardened runtime. A recording that signs nothing
shows that an identity name with spaces and parentheses reaches each call as one
argument and that no credential in the environment is printed. It also checks
that Mach-O code in `Contents/Resources` stops the script and that a missing app
is refused. The test makes no certificate or keychain, so it does not show that
a Developer ID certificate signs, that the timestamp server answers or that
Apple accepts the app. Run it with
`ctest --test-dir build -R test_sign_macos --output-on-failure`.

### macOS notarization script

`tools/release/notarize_macos.sh <app or disk image>` notarizes a file that is
signed with a Developer ID Application identity. It stops when the signature is
not one, sends the file to Apple with `xcrun notarytool submit --wait` (an app
as a zip archive made with `ditto`, an image as it is) and requires the verdict
`Accepted`. For any other verdict it prints Apple's log of the submission and
fails. Then it staples the ticket with `xcrun stapler staple`, tried up to five
times because Apple may not hand out the ticket at once, and checks it with
`xcrun stapler validate` and `spctl --assess` (`--type execute` for an app,
`--type open --context context:primary-signature` for an image). Any failing
command fails the script.

The credentials come from the environment and nowhere else. The script uses
either an App Store Connect API key, with `MACOS_NOTARY_KEY` (the text of the
`.p8` file), `MACOS_NOTARY_KEY_ID` and `MACOS_NOTARY_ISSUER_ID`, or an Apple ID,
with `MACOS_NOTARY_APPLE_ID`, `MACOS_NOTARY_TEAM_ID` and `MACOS_NOTARY_PASSWORD`
(an app-specific password). The API key wins when both are complete. With none
of the six set, or all of them empty, the script says that it skipped the file
and succeeds, so the file is not notarized. With some set and neither group
complete it fails and names the missing variables. It prints no value, and it
writes the key to a file in a private temporary folder that it removes on every
exit.

In the workflow, the credentials belong to the environment `macos-signing`: the
key and the password are environment secrets, the other four are environment
variables. Two steps read them and nothing else does. **Notarize the app** runs
after **Start the package** and before **Build the disk image**, so the app in
the image already carries its ticket and keeps it when it is copied out of the
image offline. After the image is built, **Sign the disk image** signs it with
the identity of the import step (`codesign --force --timestamp --sign`), or
leaves it unsigned when there is no identity, and **Notarize the disk image**
sends it as a second submission. Both notarization steps run only for a push to
`production` or a `v*` tag, have a time limit of 60 minutes, and have no
condition on a credential and no `continue-on-error`: a release job without
credentials succeeds and ships files that are not notarized, and one with
credentials fails when any command of the script fails, so a file that Apple
rejected is never published as notarized. **Verify the disk image** then checks
the signature of the app in the finished image as before.

The test is `tools/release/test_notarize_macos.sh`. It puts stand-ins for
`codesign`, `xcrun` and `spctl` in front of the real tools, so nothing reaches
Apple, and uses the real `ditto`. It checks the arguments and the order of the
calls for an app with an API key and for an image with an Apple ID, that the app
goes to `notarytool` as an archive that holds it and the image as it is, that the
key file has the mode 600 and is gone afterwards, that the script skips without
credentials and fails for partial ones, wrong calls, a file without a Developer
ID signature, a verdict of `Invalid`, no verdict, a failing `notarytool`, a
ticket that `stapler` hands out only at the third attempt or never, a ticket that
does not validate and a refusal by Gatekeeper, and that no value of a credential
reaches the output. It runs on macOS only. The package job runs it in the step
**Test the notarization script**, and it is not registered in CTest. It does not
show that Apple accepts the app, that the ticket attaches to the files or that
Gatekeeper accepts them: the first run with real credentials is the first proof of
that. Run the test with `bash tools/release/test_notarize_macos.sh`.

### Installer script

`installer/egtrain.iss` is the Inno Setup script of the Windows installer.
Compile it on Windows with Inno Setup 6.3 or newer:

```text
ISCC.exe /DVersion=X.Y.Z /DPayload=<package directory> [/O<output directory>] installer\egtrain.iss
```

`/DVersion` is the release version. `/DPayload` is the absolute path of the
assembled Windows package, the directory that the Windows package job uploads as
`QEGTRAIN-windows-x64-payload`. Setup writes `EGTRAIN-Setup-<version>-x64.exe`,
the name in the table of accepted names above, to `installer/Output` or to the
`/O` directory. [installer/README.md](../../installer/README.md) describes the
parameters, what the installer does and the Setup switches for a silent install.

The installer is for the current user and needs no administrator rights. It
installs the package directory as it is into `%LOCALAPPDATA%\Programs\EGTRAIN`
by default, adds a Start Menu entry and an optional desktop shortcut, and
registers an uninstall entry under the current user. It also associates the
`.egscene` file type with `{app}\QEGTRAIN.exe` for the current user, which the
uninstaller removes. It replaces the `Scenes` folder of the installation folder
on every install and runs no program. The
Windows finalize job of `package.yml` compiles the script, and
`tools/release/installer_smoke.ps1` installs the result on the runner.

The test is `tools/release/test_installer_script.py`. It reads the script,
`installer/installed-by-setup.ini`, `installer/README.md` and `kAssetPatterns`
in `update/ReleaseInfo.cpp` as text, looks for the icon file, and checks that:

- the script is ASCII and names neither `HKEY_LOCAL_MACHINE` nor a parent folder;
- it has only the sections `Setup`, `Tasks`, `Files`, `InstallDelete`, `Icons`
  and `Registry`, so no code, run or uninstall-delete section, and uses only
  the constants `{app}`, `{userpf}`, `{userprograms}` and `{userdesktop}`, so
  nothing is installed for all users, and only the two shortcuts lie outside
  the installation folder;
- the preprocessor lines are the `#ifndef` and `#error` guards for the two
  parameters, and the only macros are `{#Version}` and `{#Payload}`;
- the `[Setup]` directives that decide the scope have their pinned values: the
  `AppId`, the per-user privilege level with no override, the per-user folder,
  the 64-bit architecture, closing a running program and the refresh of the
  file associations (`ChangesAssociations`). The directives
  `PrivilegesRequiredOverridesAllowed`, `UninstallFilesDir`,
  `CreateUninstallRegKey` and `Uninstallable` are refused, and the icon file
  exists;
- the output name is one that `kAssetPatterns` accepts for
  `windows-x64-installer`;
- the package is installed as it is, with no external or downloaded file, and
  the marker file is installed next to the program and is not empty;
- only `{app}\Scenes` is deleted before an install;
- the desktop shortcut belongs to a task that is unchecked by default, and the
  only shortcuts are the two that run `{app}\QEGTRAIN.exe`;
- the `[Registry]` entries are the four of the `.egscene` file type, all for
  the current user under `Software\Classes`, and no others, so no other
  extension such as `.zip` is claimed: the default value of `.egscene` is
  `EGTRAIN.Scene` and is deleted on uninstall, with the key only when nothing
  else is left in it, so that a value of another program stays; the key
  `EGTRAIN.Scene`, its `DefaultIcon` and its `shell\open\command` are deleted
  on uninstall; the icon is the first icon of `{app}\QEGTRAIN.exe`; the open
  command is `"{app}\QEGTRAIN.exe" "%1"`;
- the README names `/DVersion`, `/DPayload` and `installed-by-setup.ini`.

The test also runs the rules on the script with CRLF line ends, as a checkout on
Windows may have them, and changes the script one line at a time to check that
each rule reports the change. It does not compile the script or run an
installer, so a green run shows nothing about Inno Setup accepting the file or
about an install on Windows. Run it with
`ctest --test-dir build -R test_installer_script --output-on-failure`.

### Installer smoke

`tools/release/installer_smoke.ps1` installs, upgrades and uninstalls the compiled
installer on Windows. The Windows finalize job runs it on the installer and on a
second installer of the next patch version, both built from the same directory:

```powershell
tools/release/installer_smoke.ps1 -Installer EXE -UpgradeInstaller EXE -Payload DIR -Version X.Y.Z -UpgradeVersion X.Y.Z [-Work DIR] [-Python NAME]
```

Run it only on a machine without EGTRAIN. It stops when an uninstall entry, a
Start Menu or desktop shortcut, `%LOCALAPPDATA%\Programs\EGTRAIN` or a key of
the `.egscene` file type (`HKCU\Software\Classes\.egscene` or
`HKCU\Software\Classes\EGTRAIN.Scene`) exists already. It changes the
registry, the Start Menu and the desktop of the current user and removes what it
installs. `-Work` must not exist or must be empty; the script puts a copy of
the package, its installation folders, a file outside them and the logs of Setup
and the uninstaller below it. It checks:

- both installers request `asInvoker` in their manifest;
- a copy of the package directory, started with `package_start_smoke.py` as a
  portable copy is, leaves no key of the `.egscene` file type in the registry;
- a silent install (`/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /LOG`) into a folder
  with `/DIR` and without the desktop task leaves one uninstall entry under
  `HKEY_CURRENT_USER` that names the folder and the version, none under
  `HKEY_LOCAL_MACHINE`, a Start Menu shortcut that starts `QEGTRAIN.exe` in the
  folder, no desktop shortcut, and in the folder the files of the package with the
  same bytes, `installed-by-setup.ini` and one uninstaller, and nothing else.
  The default value of `HKCU\Software\Classes\.egscene` is `EGTRAIN.Scene`,
  and the key `EGTRAIN.Scene` has a display name, the first icon of the
  installed `QEGTRAIN.exe` and the open command `"<folder>\QEGTRAIN.exe" "%1"`;
  it is not under `HKEY_LOCAL_MACHINE`.
  `package_start_smoke.py` then starts the installed copy. The silent uninstall
  removes the entry, the shortcut, the file type and the files, and a file beside
  the folder survives. A file that the program wrote into the folder during the
  start check may stay with the folder;
- an install with `/TASKS=desktopicon` also creates the desktop shortcut, and the
  uninstall removes it. The script first plants a value of another program in
  `HKCU\Software\Classes\.egscene`. It must survive the install and the
  uninstall, which removes only the default value of that key. After the other
  uninstalls the key is gone;
- an install into the default folder, the second installer over it and an
  uninstall: the upgrade leaves one entry and one folder, the entry shows the
  higher version, a scene folder that the package does not hold is gone and the
  files are those of the package. The uninstall leaves a file outside the folder.

The script does not use `/CURRENTUSER`, because the installer script offers no
choice of scope and the switch would change nothing. The account of a CI runner
is an administrator, so a run there does not show that a standard user installs
without an elevation request. The script compares bytes with `fc.exe` and
computes no hash.

### Signature check

`tools/release/verify_signatures.ps1` checks the Authenticode signatures of the
Windows program files in a directory:

```powershell
tools/release/verify_signatures.ps1 -Directory DIR (-Require | -Report) [-ExpectedSubject SUBJECT] [-Name FILE,...] [-AllowUntrustedRoot] [-NoTimestampCheck]
```

A file passes when its status is `Valid`, the digest algorithm of its signature
is SHA-256, the signature has a timestamp and the subject of the signing
certificate equals `-ExpectedSubject`. `-Name` defaults to `QEGTRAIN.exe`,
`egtrain_update_helper.exe` and `scene_tool.exe`. `-Require` needs
`-ExpectedSubject` and exits with 1 when a file fails; `-Report` prints the
findings and exits with 0. The Windows package job runs it with `-Report` on the
unsigned package of every run, and the two signing jobs run it with `-Require`:
`sign-windows` on the payload, and `sign-windows-installer` on the signed installer
(named with `-Name`) and on the payload. The
other two switches are for tests that sign with a throwaway certificate and must
not be used on a release.

`test_verify_signatures` (`tools/release/test_verify_signatures.ps1`) runs on
Windows only. It creates a self-signed code-signing certificate in the store of
the current user, signs copies of the built `scene_tool.exe` and runs the script
on them in a new PowerShell. A copy signed with SHA-256 must pass. An unsigned
copy, a copy signed with SHA-1, a check for another subject, a missing file, a
check without `-AllowUntrustedRoot` and a check without `-NoTimestampCheck`
(the copies have no timestamp) must fail, and the output must name the reason.
The certificate is never exported or added to a trusted root, and the test
removes it with its key. Run it by hand with
`ctest --test-dir build -C Release -R test_verify_signatures --output-on-failure`.

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
