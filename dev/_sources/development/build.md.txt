# Build from source

This page lists what you need to build EGTRAIN and explains how to configure, build and run it, with the
common setup failures and their fixes.

Run configure, build, and test commands from the repository root. Run the
application itself from `EGTRAIN/QEGTRAIN` so relative scene paths resolve.

## Requirements

- CMake 3.16 or newer, or 3.21 or newer for the presets in `CMakePresets.json`
- C++17 compiler
- Qt 6 Core, Gui, Widgets, Charts, Svg, Network and Help, and the Qt tools
  (`qhelpgenerator` builds the manual). CI and the release packages use Qt 6.8.3.
- ZeroMQ, cppzmq, and nlohmann-json
- Python 3.9 or newer, found by `find_package(Python3)` at configure time;
  CTest runs its Python tests with that interpreter. On Windows, set
  `PYTHONUTF8=1` for local runs so scripts read program output as UTF-8;
  the Windows presets set it.
- Git, to clone the repository. On Windows, Git for Windows is a separate install from Visual Studio, and vcpkg
  uses it to fetch its registry at the `builtin-baseline` of `tools/vcpkg/vcpkg.json`.

Qt Svg is required by the application and must be available with the
other Qt modules. A configure that finds no Qt 6 with these modules stops with a message
that names Qt 6.

Supported toolchains, as used by CI:

- macOS: Qt 6.8.3 and the Homebrew packages in the configure example below.
- Windows 10 or 11, x64 only: MSVC (Visual Studio 2022 or newer), Qt 6.8.3
  `msvc2022_64` with Qt Charts, and the vcpkg manifest `tools/vcpkg/vcpkg.json` (ZeroMQ, cppzmq and
  nlohmann-json at its `builtin-baseline`) for the `x64-windows` triplet. 32-bit Windows is not supported.
- Linux: Ubuntu 24.04 (the `ubuntu-24.04` runner image) with Qt 6.8.3 and the apt
  packages in the configure example below. The AppImage needs the glibc of the
  system that builds it, so CI builds and tests on 24.04 and not on `ubuntu-latest`.

CI installs Qt 6.8.3 with the Qt Charts module through `jurplel/install-qt-action`, which runs
[aqtinstall](https://aqtinstall.readthedocs.io/en/latest/getting_started.html). The same
installer works on a developer machine; the [Qt online installer](https://www.qt.io/download-qt-installer-oss)
is the other way (choose Qt 6.8.3 with the Qt Charts module). Homebrew's `qt` formula follows the
newest Qt release, not the 6.8.3 of CI.

aqtinstall goes into a virtual environment, because the Python of Ubuntu 24.04 and of Homebrew does not
take packages from `pip` directly. On Windows the commands are in `~\aqt-venv\Scripts` instead of
`~/aqt-venv/bin`.

```bash
python3 -m venv ~/aqt-venv
~/aqt-venv/bin/pip install aqtinstall
~/aqt-venv/bin/aqt install-qt mac desktop 6.8.3 clang_64 -m qtcharts -O "$HOME/Qt"          # macOS: ~/Qt/6.8.3/macos
~/aqt-venv/bin/aqt install-qt linux desktop 6.8.3 linux_gcc_64 -m qtcharts -O "$HOME/Qt"    # Linux: ~/Qt/6.8.3/gcc_64
~/aqt-venv/bin/aqt install-qt linux_arm64 desktop 6.8.3 linux_gcc_arm64 -m qtcharts -O "$HOME/Qt"  # arm64 Linux: ~/Qt/6.8.3/gcc_arm64
~/aqt-venv/bin/aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 -m qtcharts -O C:\Qt  # Windows: C:\Qt\6.8.3\msvc2022_64
```

## Configure

```bash
cmake -S . -B build -DEGTRAIN_BUILD_TESTS=ON -DCMAKE_PREFIX_PATH=<Qt 6 folder>
```

On macOS, with Qt in `~/Qt/6.8.3/macos`:

```bash
brew install zeromq cppzmq nlohmann-json
cmake -S . -B build -DEGTRAIN_BUILD_TESTS=ON \
  -DCMAKE_PREFIX_PATH="$HOME/Qt/6.8.3/macos;$(brew --prefix zeromq);$(brew --prefix cppzmq);$(brew --prefix nlohmann-json)" \
  -DWrapOpenGL_AGL="$(xcrun --show-sdk-path)/System/Library/Frameworks/OpenGL.framework"
```

The prefix path names the keg folders of the other libraries, not `/opt/homebrew`, whose include folder
can hold the headers of another Qt. Qt 6.8.3 links the AGL framework, which the macOS 26 SDK no longer
has; `WrapOpenGL_AGL` links the OpenGL framework in its place.

On Ubuntu, with Qt in `~/Qt/6.8.3/gcc_64`:

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake git python3 python3-venv libgl-dev \
  libzmq3-dev cppzmq-dev nlohmann-json3-dev
cmake -S . -B build -DEGTRAIN_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$HOME/Qt/6.8.3/gcc_64"
```

To open the window, the X11 platform plugin of that Qt also needs `libxcb-cursor0`.

In a root shell, such as a plain `ubuntu:24.04` container, `sudo` is not installed: leave it out.

Ubuntu 24.04 also ships Qt 6.4.2. It is enough: the application and all tests build with it, also with
warnings as errors, and the tests pass. Add its packages to the install line and leave out the prefix path:

```bash
sudo apt-get install -y build-essential cmake git python3 python3-venv libgl-dev \
  libzmq3-dev cppzmq-dev nlohmann-json3-dev \
  qt6-base-dev qt6-charts-dev qt6-svg-dev qt6-tools-dev qt6-qpa-plugins libqt6sql6-sqlite
cmake -S . -B build -DEGTRAIN_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release
```

The configure step then prints `Could NOT find XKB` and `Could NOT find PkgConfig`, which do no harm. CI and
the packages use 6.8.3, so a difference between the two shows only there.

On macOS and Linux, `CMakePresets.json` also has the presets `macos-debug`, `macos-release`, `linux-debug` and
`linux-release`. They build with Ninja (`brew install ninja`, or `sudo apt-get install -y ninja-build`) and with the
tests, each in `out/build/<preset>`. The libraries come from the configure line, as above; the build folder keeps
them, so later configures need only the preset:

```bash
cmake --preset macos-release \
  -DCMAKE_PREFIX_PATH="$HOME/Qt/6.8.3/macos;$(brew --prefix zeromq);$(brew --prefix cppzmq);$(brew --prefix nlohmann-json)" \
  -DWrapOpenGL_AGL="$(xcrun --show-sdk-path)/System/Library/Frameworks/OpenGL.framework"
cmake --build --preset macos-release
ctest --preset macos-release
```

On Linux, use `linux-release` and leave out `WrapOpenGL_AGL`. A preset build puts the application in
`out/build/<preset>`, not in `build/`. CTest gives the smoke tests that path; a smoke script run by hand needs it in
`QEGTRAIN_APP` (see [Platform coverage](testing.md#platform-coverage)).

On Windows, `CMakePresets.json` holds the settings: Ninja and MSVC for x64, the vcpkg of Visual Studio with the
`x64-windows` triplet and the manifest `tools/vcpkg/vcpkg.json`, Qt from `C:/Qt/6.8.3/msvc2022_64`, and the tests.
From an x64 Developer PowerShell for Visual Studio 2022 or newer, in the repository folder:

```powershell
cmake --preset x64-release
cmake --build --preset x64-release
ctest --preset x64-release
```

The `x64-debug` presets make a Debug build. Each preset builds in `out\build\<preset>`. The first configure on a
machine also builds ZeroMQ, cppzmq and nlohmann-json with vcpkg; later configures restore them from the cache of vcpkg
in `%LOCALAPPDATA%\vcpkg\archives`. The configure prints `Could NOT find WrapVulkanHeaders` twice, which does no
harm. The Windows leg of CI (`.github/workflows/cmake.yml`) and the Windows package (`.github/workflows/package.yml`)
install the same manifest with the vcpkg of the runner image: the leg through the `ci-windows` preset, and the package
job with the same options on its configure line.

The `ci-macos`, `ci-linux` and `ci-windows` presets hold the options that the three CI legs share: a Release build with
the tests, `EGTRAIN_WARNINGS_AS_ERRORS=ON` and `EGTRAIN_IPO=OFF`, in `build/`. `ci-windows` adds Ninja, `cl` and
`EGTRAIN_NETWORK_TESTS=ON`. Each configure step of the workflow adds the compiler launcher and the prefix path of its
runner; see [Configure options](ci-and-release.md#configure-options).

The Start-menu entry **Developer PowerShell for VS** opens a shell that targets 32-bit Windows, and the configure then
stops with a message that names x64 as the only supported platform. Switch that shell to x64 first.
`-VsInstallPath` is the folder of Visual Studio, here Visual Studio Community 2026; on an Arm64 PC use `-host_arch=arm64`:

```powershell
Enter-VsDevShell -VsInstallPath 'C:\Program Files\Microsoft Visual Studio\18\Community' -SkipAutomaticLocation `
  -DevCmdArguments '-arch=x64 -host_arch=x64'
```

`Qt6_DIR` in the `CMakeCache.txt` of a build folder (`out\build\<preset>` with a preset) shows which Qt it uses.

Without a preset, CMake on Windows uses the Visual Studio generator, which is multi-config: choose the configuration
when building and testing, as in the next two sections.

### Visual Studio

The repository has no Visual Studio solution or project file. Visual Studio opens the folder as a CMake project and
uses the presets:

1. Install Visual Studio 2022 or newer with the **Desktop development with C++** workload. Its default components
   include the C++ CMake tools and the vcpkg package manager.
2. Install Git for Windows and Python 3.9 or newer. Install Qt 6.8.3 `msvc2022_64` with Qt Charts into `C:\Qt`, with
   aqtinstall or the Qt online installer as in [Requirements](#requirements).
3. Open the repository folder with **File > Open > Folder**. Visual Studio then configures the `x64-debug` preset. The
   first configure on a machine also builds the vcpkg libraries and takes longer.
4. For a Release build, pick `x64-release` in the configuration list of the toolbar.
5. Pick `QEGTRAIN.exe` as the startup item, not `QEGTRAIN.exe (Install)`, and press F5. The default startup item is
   another program of the build.
6. Run the tests from **Test > Test Explorer**.

Visual Studio and `ctest --preset` run the programs and tests with the environment of the preset, whose `PATH` has the
Qt `bin` folder. For Qt in another folder, add a `CMakeUserPresets.json` next to `CMakePresets.json` (Git ignores it)
and pick its preset in step 4:

```json
{
  "version": 3,
  "configurePresets": [
    {
      "name": "my-debug",
      "inherits": "x64-debug",
      "environment": { "QT_ROOT_DIR": "D:/Qt/6.8.3/msvc2022_64" }
    }
  ]
}
```

## Build

```bash
cmake --build build
```

With a multi-config generator, add the configuration:
`cmake --build build --config Release`.

### Build structure

The scene, simulation, utility and live frame and server code is built once as static
libraries: `egtrain_scene`, `egtrain_sim`, `egtrain_util` and
`egtrain_live`. The Qt code that several targets use is
built once as well: `egtrain_graphics`, `egtrain_widgets`, `egtrain_http`, `egtrain_update`,
`egtrain_sceneopen` (`SceneOpenRequest`), `egtrain_openrelay` (`OpenRequestRelay`), `egtrain_telemetry`, `egtrain_dispatch` (`DispatchController`,
`SimulationWorker` and `RunControl`) and `egtrain_diagrams`. `QEGTRAIN`, `scene_tool` and the
tests link them instead of listing their sources, so each file is compiled
once. A test gets only the library members it references. `egtrain_recovery` holds
`RecoveryStore`, `RecoveryController` and `RecoveryPrompt`. `QEGTRAIN` links it, and so do
`test_recoverystore`, `test_recoverycontroller` and `test_recoveryprompt`.

Four things are still compiled more than once. The telemetry sources are built
again with test hooks as `egtrain_telemetry_hooks` when tests are built, and as
`egtrain_telemetry_smoke` for the application in the isolated telemetry smoke
configuration, because the hooks change the layout of the telemetry classes.
`TelemetryConsentDialog.cpp` is compiled in `QEGTRAIN` and in
`test_telemetryconsent` for the same reason. `update/SelfUpdater.cpp` is compiled
in `QEGTRAIN` and in `test_selfupdater` with `EGTRAIN_UPDATE_TEST_HOOK`, which adds
`SelfUpdater::setTestHook`. `app/UpdateTelemetryController.cpp` is compiled in `QEGTRAIN` and in
`test_updatetelemetrycontroller`, together with `TelemetryConsentDialog.cpp` and `SelfUpdater.cpp`. See
[Source layout](../architecture/source-layout.md#libraries).

With `EGTRAIN_BUILD_TESTS=ON`, `QEGTRAIN` also compiles the sources in
`EGTRAIN/QEGTRAIN/tests/e2e`, the runner for the scripted GUI scenarios. At the
start of `main()` it redirects the user settings to the folder named by
`QEGTRAIN_E2E_SETTINGS_DIR`. After the first show of the window it starts a
scenario, with blocking dialogs switched off, when the variable of that scenario
is set. A build without tests, such as the three package builds, compiles none
of it: in such a build the first show of the window starts no scenario and
`QEGTRAIN_E2E_SETTINGS_DIR` redirects no settings. `MainWindowTestAccess` is the
friend class of `MainWindow` that the runner calls, and of `SceneDocument`, whose revision a scenario sets when it
replaces the scene without an edit, of `SceneSession`, whose scene folder and diagnostics a scenario sets, of
`UpdateTelemetryController`, whose settings, consent and sender the telemetry smoke sets, and of `RunController`, whose
worker, status and results a scenario reads and sets.
A scenario is a member of
`MainWindowTestAccess` defined in its own file in that folder (the legacy import
smoke is `LegacyImport.cpp`, the scene drop smoke `SceneDrop.cpp`, the output folder
smoke `OutputFolder.cpp`, the scene render smoke `SceneRender.cpp`, the station
overlay smoke `StationOverlay.cpp`, the track preview smoke `TrackPreview.cpp`, the
visual polish smoke `VisualPolish.cpp`, the editor smoke `EditorSmoke.cpp`, the creator
acceptance smoke `CreatorAcceptance.cpp`, the crash and recovery runs `Recovery.cpp`). The
visual polish smoke also starts from the frames of a run: `renderSnapshot` calls the
`frameRendered` hook of `MainWindowTestHooks` after every frame, and the hook is set only
when `QEGTRAIN_E2E_VISUAL_POLISH` is set. The editor smoke sets the `passengerSource` hook
to the folder that Import Passengers reads in place of its dialog, and clears it afterwards.
The creator acceptance smoke starts without a scene: when `QEGTRAIN_E2E_CREATOR_ACCEPTANCE`
is set, `main()` opens the window through `e2e::runCreatorAcceptanceMain`, the first show asks
no startup question (`suppressStartupPrompts`), and capacity analysis shows its scope dialog
(`askCapacityScope`), which the scenario answers. The scenario opens the Time / distance window after the baseline
run, runs two more scenarios, and then checks that the window still exports the baseline run and not the run that the
application holds by then. A scenario keeps its state, such as whether it
has begun its checks, in `MainWindowTestAccess`, not in the window.

`Dialogs.cpp` answers the dialogs that the window opens. `showStartupChooser`, `showRunReview` and
`UpdateTelemetryController::showPrivacySettings` call the `dialogShown` hook of `MainWindowTestHooks` with the dialog just before
it opens, and the first two call `dialogClosed` once they have read the answer. With
`QEGTRAIN_E2E_STARTUP_CHOOSER` set, the runner records the rows and menus of the case chooser, takes the
action that `QEGTRAIN_E2E_CHOOSER_INTERACTION` names on the row of `QEGTRAIN_E2E_CHOOSER_PATH`, and prints
the `E2E_STARTUP_CHOOSER_` lines when the chooser has closed. With `QEGTRAIN_E2E_REVIEW_CANCEL` set, the
run review opens although dialogs are suppressed (`askRunReview`). The runner inspects it, cancels it with
the Cancel button or, for the value `escape`, with the Escape key, and prints the `E2E_RUN_REVIEW_`
lines; `QEGTRAIN_E2E_REVIEW_ENLARGED` enlarges the fonts first. With `QEGTRAIN_E2E_PRIVACY_DIALOG` set
to `1` or `settings` and `QEGTRAIN_E2E_SETTINGS_DIR` set, the `privacyPreview` hook gives the privacy
dialog a fake context, no telemetry starts, and `QEGTRAIN_E2E_SCREENSHOT` names the file that receives a
picture of the dialog. In the isolated telemetry smoke configuration (`EGTRAIN_ISOLATED_TELEMETRY_SMOKE`,
described in [Telemetry](../telemetry/README.md)) `TelemetrySmoke.cpp` is compiled too. Its hooks
`telemetryStart` (which gets the `UpdateTelemetryController` of the window), `observationStep`, `runStarting` and `windowDestroyed` start the telemetry in the
folder that `QEGTRAIN_E2E_TELEMETRY_SMOKE` names, fail the steps that install the observation of a run,
stop a run at the point that the mode names, and print `E2E_TELEMETRY_SHUTDOWN`; the actions of each mode
are in `runTelemetrySmoke`, which `tools/e2e/telemetry_smoke.py` drives.

`test_no_e2e_in_product` runs `tools/e2e/test_no_e2e_in_product.py`. It fails when a source, header or
form that a build without tests compiles holds `QEGTRAIN_E2E_`, `E2E_` or `failures <<`. The files under
`tests/e2e` and the telemetry smoke support are not scanned. Four files are exempt for the text that
they hold on purpose: `update/UpdateSettings.cpp` and `telemetry/TelemetryConsent.cpp` test the prefix
`QEGTRAIN_E2E_`, and `app/OpenRequestRelay.cpp` and `app/OpenRequestRelay.h` test it and name
`QEGTRAIN_E2E_OPEN_RELAY`. The test also fails when a script under `tools/` waits for an `E2E_` marker that
no C++ file in `tests/e2e` or `app` prints, such as a misspelt name. That check skips the `test_*.py` files, whose
markers are fixtures.

`PostRun.cpp` holds the autostart and the checks that follow a run. With `QEGTRAIN_AUTOSTART`
set, the runner starts the run of the case 1.5 s after the first show. The hooks
`snapshotDelivered`, `snapshotDrawn`, `runResultsRefreshed`, `runFinished`, `runSettled` and
`routeDiagramBuilt` of `MainWindowTestHooks` call the checks of `QEGTRAIN_E2E_EXPORT_DIR`,
`QEGTRAIN_E2E_ROUTE_DIAGRAM`, `QEGTRAIN_E2E_SIGNAL_HEADS`, `QEGTRAIN_E2E_PAUSE_STEPS`,
`QEGTRAIN_E2E_OPERATIONAL_COMPLETION`, `QEGTRAIN_E2E_OPERATIONAL_DISCARD`,
`QEGTRAIN_E2E_REPLAY_WHOLE_RUN` and `QEGTRAIN_E2E_CLOSE_DURING_RUN`, and the markers
`E2E_GUI_AUTOSTART_RUNNING` and `E2E_GUI_RUN_RESULTS`. A build without tests compiles none of it,
so these variables start no run and run no check there.

The hook of `QEGTRAIN_E2E_EXPORT_DIR` also opens the blocking-time diagram, the delay diagram and the four
per-train diagrams. For the blocking-time, the delay and the speed vs distance window it writes the CSV
that the window exports with all trains, with the first train and with none (`<window>_window_all.csv`,
`_one`, `_none`). For the blocking-time, the delay and the per-train diagrams it also writes a text dump of the
chart (`blocking_time_series.txt`, `delay_diagram_series.txt`, `speed_distance_series.txt` and so on: the axes,
then every series with its properties and points). Two builds of one scene give the same files, so `cmp`
shows whether a change moved a diagram.

The hook of `QEGTRAIN_E2E_ROUTE_DIAGRAM` builds the train paths and the timetable graph with each route that trains
run on as reference. For each of them it writes the text dump of the chart and the CSV of the window with all trains
(`train_path_graph_route<N>_series.txt`, `train_path_graph_route<N>.csv`, and `timetable_graph_route<N>` likewise, with
`N` the index of the route in the run).

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
`..\..\build\QEGTRAIN.exe`, and a preset build at `..\..\out\build\x64-release\QEGTRAIN.exe`.
Started outside Visual Studio, it needs the Qt `bin` folder on `PATH`, for example
`$env:PATH = "C:\Qt\6.8.3\msvc2022_64\bin;$env:PATH"`. Without it the program exits with no window or message.
These are local build paths; downloaded release packages are documented in [Installing EGTRAIN](../guides/install.md).

Runtime output defaults to
`<QStandardPaths::AppDataLocation>/Output/<scene>`. It does not always use a
repository `Output/` directory. Set `QEGTRAIN_OUTPUT_DIR` to override the base
directory; EGTRAIN then writes `<override>/Output/<scene>`.

## Common setup failures

Configure, build and test problems that have come up, with the text CMake or the tool prints. The wording
of CMake and CTest messages varies with the CMake version. The Qt message, the wrong-source-directory
message, `No tests were found!!!` and the missing-target error were reproduced on macOS, and the x64-only
message, the symbolic link failures and the quarantined test on Windows. The other rows were checked
against `CMakeLists.txt` and the scripts but not run.

| Symptom | Cause | Fix |
| --- | --- | --- |
| `EGTRAIN needs Qt 6 with the Charts, Svg and Help modules` | CMake does not look in the folder of Qt 6, or found only Qt 5 | Add the Qt 6 folder to `CMAKE_PREFIX_PATH`, for example `~/Qt/6.8.3/macos` or `C:/Qt/6.8.3/msvc2022_64`. CMake keeps the Qt it found first, so after a wrong first configure delete the `build` folder and configure again |
| `Could not find a package configuration file provided by "Qt6Charts"` (or `Qt6Svg`), followed by the message above | The module is not installed | `-m qtcharts` for aqtinstall or the Qt Charts module in the Qt online installer; `qt6-charts-dev qt6-svg-dev` with the Qt of Ubuntu |
| `Could not find a package configuration file provided by "cppzmq"` or `"nlohmann_json"` | The package is not installed, or on Windows vcpkg is not given to CMake | Install it as in [Configure](#configure). On Windows use a preset, or pass `CMAKE_TOOLCHAIN_FILE` and `VCPKG_TARGET_TRIPLET=x64-windows` |
| `Could not find toolchain file: "/scripts/buildsystems/vcpkg.cmake"` | `VCPKG_ROOT` is not set: the shell is not a Developer PowerShell, or Visual Studio lacks the vcpkg package manager | Use an x64 Developer PowerShell, and add the vcpkg package manager in the Visual Studio Installer |
| `Could NOT find Python3` | No Python 3.9 or newer was found: it is missing from `PATH`, or the one found is older (CMake then names the version it found). Only a configure with `-DEGTRAIN_BUILD_TESTS=ON` looks for it | Install Python 3.9 or newer, or pass `-DPython3_EXECUTABLE=<path>` |
| `The source ".../CMakeLists.txt" does not match the source ".../CMakeLists.txt" used to generate cache` | The `build` folder was configured for another checkout, for example a copied folder or another Git worktree | Use one build folder per checkout, for example `-B build-feature`, or delete the old one |
| `EGTRAIN supports 64-bit Windows (x64) only` | A 32-bit generator, toolset or Developer PowerShell, such as the one of the Start menu | Configure for x64: `-A x64` with a Visual Studio generator, an x64 configuration in Visual Studio, or a preset in a Developer PowerShell switched to x64 with `Enter-VsDevShell` as in [Configure](#configure) |
| `No tests were found!!!` from `ctest`, or `No rule to make target 'test_timeformat'` | The build folder was configured without `-DEGTRAIN_BUILD_TESTS=ON`, which is off by default | Run the configure command again with the option, in the same folder |
| `Test not available without configuration.  (Missing "-C <config>"?)` from `ctest` | A multi-config generator such as Visual Studio | Add `-C Release` to `ctest`, and `--config Release` to `cmake --build` |
| `test_playback_profile` and `test_scenesavepolicy` fail on Windows with `A required privilege is not held by the client` | The tests create symbolic links, which a standard user can create only with Developer Mode on | Turn on Developer Mode in **Settings > System > For developers**, which needs an administrator |
| `Could not find executable` or `Process not started` from `ctest` for a test that was just built | Microsoft Defender quarantined the test program as a false positive | Build that test again with `cmake --build --preset <preset> --target <test>`, with the preset that `ctest` ran. **Windows Security > Protection history** lists the detection |
| The build takes very long | `cmake --build build` runs one job with the Makefile generator, and a Release link uses link-time optimization | Add `--parallel 4` (or set `CMAKE_BUILD_PARALLEL_LEVEL`), and configure with `-DEGTRAIN_IPO=OFF` for faster links |
| `clang-format ... is not installed`, or `... is clang-format X, but the sources are formatted with 23.1.3` from `tools/format.py` | The script accepts one release only | `python3 -m venv ~/fmt-venv && ~/fmt-venv/bin/pip install clang-format==23.1.3`, then put `~/fmt-venv/bin` on `PATH` or name the executable in `CLANG_FORMAT` |
| Python scripts fail on Windows with a decoding error | The console code page is not UTF-8 | Set `PYTHONUTF8=1` |
| The application starts but a relative `--scene` path or `Scenes/...` is not found | The working directory is not `EGTRAIN/QEGTRAIN` | Start it from that folder, or give an absolute path |
