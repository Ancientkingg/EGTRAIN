# Getting started as a developer

This page takes you from a fresh clone to a built application, a first run, a changed test and the
checks a pull request has to pass. It points to the longer guides for details. EGTRAIN is a Qt 6 and
C++17 desktop application for railway micro-simulation, built with CMake.

The commands were last run from a fresh clone on macOS (Apple silicon) on 2026-10-17; the macOS build was repeated with
Qt 6.8.3 on 2026-10-20. On Linux the page was followed from a clean clone in a new Ubuntu 24.04 arm64 container with
10 CPUs on 2026-10-20, with the Qt 6.4.2 packages of Ubuntu: installing the packages took 36 s, configuring under
1 s, the build with four jobs 4 min 7 s, CTest 3 min 8 s (325 of 325 passed), the headless run of all six cases
1 min 49 s, the round-trip smoke 7 s, the visual polish smoke 4 min 13 s and the test walkthrough below under a
minute. The build with warnings as errors passed as well. On Windows the [presets](build.md#configure) were followed
on 2026-10-21 in a Windows 11 Arm64 VM with Visual Studio Community 2026, from an x64 Developer PowerShell: the
configure took 94 s with the vcpkg packages restored from its cache (148 s the day before, when vcpkg built them), the
build 13 min 11 s and CTest 21 min 33 s. 317 of 319 tests passed; the other two create symbolic links and need
Developer Mode, as [Common setup failures](build.md#common-setup-failures) says. Visual Studio opened the folder,
built with the same presets and started EGTRAIN. CI installs the same packages as
[Configure](build.md#configure) lists, but it configures with more options; see
[Which checks to run](#which-checks-to-run). The Visual Studio generator path without a preset in the build guide
is not what CI uses and was not checked for this page.

## What you need

| Tool | Version | Notes |
| --- | --- | --- |
| Git | any recent release | |
| CMake | 3.16 or newer | `CMakeLists.txt` at the repository root |
| C++ compiler | C++17 | Apple Clang on macOS, MSVC 2022 or newer on Windows (x64 only), GCC on Ubuntu |
| Qt 6 | Core, Gui, Widgets, Charts, Svg, Network and Help | CI uses Qt 6.8.3 from aqtinstall on all three systems; the Qt 6.4.2 of Ubuntu 24.04 also builds |
| ZeroMQ, cppzmq, nlohmann-json | any release that CMake finds | `libzmq3-dev cppzmq-dev nlohmann-json3-dev` on Ubuntu, vcpkg on Windows |
| Python | 3.9 or newer | CMake looks for it when tests are on; CI uses 3.12 |
| clang-format | 23.1.3 exactly | Only to format C++ sources: `python3 -m venv ~/fmt-venv && ~/fmt-venv/bin/pip install clang-format==23.1.3`, then `~/fmt-venv/bin` on `PATH` |
| clang-tidy | 23.1 | Only for [`tools/tidy.py`](coding-guidelines.md#static-checks) |

[Requirements](build.md#requirements) lists the supported toolchains and the exact install
commands for each system.

## Clone, configure and build

Clone the repository and change into it:

```bash
git clone https://github.com/Ancientkingg/EGTRAIN.git
cd EGTRAIN
```

On macOS, install Qt 6.8.3 with [aqtinstall](https://aqtinstall.readthedocs.io/en/latest/getting_started.html)
(or the Qt online installer) and the other libraries with Homebrew:

```bash
python3 -m venv ~/aqt-venv
~/aqt-venv/bin/pip install aqtinstall
~/aqt-venv/bin/aqt install-qt mac desktop 6.8.3 clang_64 -m qtcharts -O "$HOME/Qt"
brew install zeromq cppzmq nlohmann-json
cmake -S . -B build -DEGTRAIN_BUILD_TESTS=ON \
  -DCMAKE_PREFIX_PATH="$HOME/Qt/6.8.3/macos;$(brew --prefix zeromq);$(brew --prefix cppzmq);$(brew --prefix nlohmann-json)" \
  -DWrapOpenGL_AGL="$(xcrun --show-sdk-path)/System/Library/Frameworks/OpenGL.framework"
cmake --build build --parallel 4
```

[Configure](build.md#configure) explains the prefix path and the AGL option.

On Ubuntu and Windows, install Qt as [Requirements](build.md#requirements) says and take the
install and configure lines from [Configure](build.md#configure); the Ubuntu line installs `git`
and `python3` too. The build, test and run commands below use the macOS and
Linux form. With a multi-config generator such as Visual Studio, add `--config Release` to
`cmake --build` and `-C Release` to `ctest`, and run the executable from
[Run a local build](build.md#run-a-local-build).

Without `--parallel`, `cmake --build` uses one job with the Makefile generator, which takes a long
time; `CMAKE_BUILD_PARALLEL_LEVEL` sets the number of jobs as well. A first full build with tests
and four jobs took about 14 minutes on one Apple silicon laptop and about 4 minutes in one Linux
container with 10 CPUs; it will differ on other machines.

A Release build, the default, links with link-time optimization. `-DEGTRAIN_IPO=OFF` at configure
time makes the links shorter.

The build makes three programs and, with `EGTRAIN_BUILD_TESTS=ON`, one executable for each test:

| Program | What it is | macOS path | Other systems |
| --- | --- | --- | --- |
| `QEGTRAIN` | The application | `build/QEGTRAIN.app/Contents/MacOS/QEGTRAIN` | [Run a local build](build.md#run-a-local-build) |
| `scene_tool` | Validates, packs, unpacks, imports and exports scenes without the window | `build/scene_tool` | `build/scene_tool` or `build/Release/scene_tool.exe` |
| `egtrain_update_helper` | Helper of the self-updater of release packages | `build/egtrain_update_helper` | |

If configure or the build stops, see [Common setup failures](build.md#common-setup-failures).

## Run the application

Run it from `EGTRAIN/QEGTRAIN`. A relative `--scene` path and the `Scenes` folder that the case chooser
lists are resolved from the working directory.

```bash
cd EGTRAIN/QEGTRAIN
../../build/QEGTRAIN.app/Contents/MacOS/QEGTRAIN
```

The window opens the Netherlands case with the **Open a Case** chooser on top. Choose a case there, or
start with `-n 2` (Paimpol) or `--scene Scenes/Paimpol` to skip the chooser; the other options are in
[Command-line options](../guides/command-line.md).

These paths are for macOS. On Linux the executable is `../../build/QEGTRAIN`. On Windows it is
`..\..\build\Release\QEGTRAIN.exe` with a multi-config generator and `..\..\build\QEGTRAIN.exe` with
a single-config one; see [Run a local build](build.md#run-a-local-build).

A run without a window needs no display and exits when the run ends. It writes to the folder that
`QEGTRAIN_OUTPUT_DIR` names. On Linux, with the executable path of that system:

```bash
mkdir -p /tmp/egtrain-out
QEGTRAIN_OUTPUT_DIR=/tmp/egtrain-out ../../build/QEGTRAIN -n 2 -g 0
ls /tmp/egtrain-out/Output/Paimpol
```

On macOS use `../../build/QEGTRAIN.app/Contents/MacOS/QEGTRAIN` in the second line. The run prints its
progress as one long line.

The files are listed in [Run output](../guides/run-output.md). On Windows a run without a window prints
nothing to a terminal; see [Windows headless runs](testing.md#windows-headless-runs).
The bundled cases and their `-n` numbers are in [Bundled case numbers](../guides/command-line.md#bundled-case-numbers).

## Source map

All C++ sources are under `EGTRAIN/QEGTRAIN/` and include each other by paths from that folder
(`#include "scene/SceneModel.h"`). [Source layout](../architecture/source-layout.md) has the full list
of folders, the libraries and the renames.

| To find | Look in |
| --- | --- |
| Start of the program, the command line | `app/main.cpp` |
| The main window, menus, docks and the glue between them | `app/MainWindow.cpp`, `app/MainWindow.h` |
| The scene editor panels and the open scene with its unsaved state | `editors/` (`SceneDocument`, `*Panel`, `infrastructure/`) |
| Loading, validating, saving, bundling, importing and exporting scenes | `scene/` (`SceneModel`, `SceneBundle`, `SceneValidator`, `SceneWriter`, `SceneImporter`, `SceneExporter`) |
| Turning a scene into a runnable simulation | `simulation/builders/` (`InfrastructureBuilder`, `OperationsBuilder`) and `app/DispatchController.cpp` |
| The simulation itself: trains, signalling, dynamics | `simulation/` |
| Results, charts, analysis windows, CSV export | `diagrams/` (`CompletedRunCapture`, `RunResults`, `RunCsv`, `BlockingTimeData`, `RouteDiagramData`) |
| The network canvas | `graphics/` and `graphics/items/` |
| Dock widgets, dialogs, small controls | `widgets/` |
| Logging, time and number formatting | `util/` |
| Live stream and legacy XML channels | `io/` |
| Tests | `tests/` (C++), `tools/e2e/` (scripts) |
| Bundled cases | `Scenes/` |
| Release, memory and performance scripts | `tools/release/`, `tools/memory/`, `tools/performance/` |

## From open to saved

This is the path of one case through the code, in the order a user takes it. It is the quickest way
into `MainWindow.cpp`. [Scene editor flow](../architecture/scene-editor-flow.md) describes the same
path from the user's side, and [Scene model](../architecture/scene-model.md) describes the data.

1. **Open.** `File > Open Case...` (a `.egscene` bundle), `File > Open Case Folder...` or the
   case chooser lead to `SceneSession::openSceneDirectory`. It probes the scene format, and the window
   loads the scene with `loadScenePath` into a `SceneModel` and hands it to its `SceneDocument`.
2. **Edit.** An editor panel changes the model in place and calls `SceneDocument::markChanged`.
   `MainWindow::sceneDocumentChanged` records the step for Undo and Redo, discards the results of an
   earlier run and schedules the recovery copy.
3. **Validate.** `MainWindow::refreshValidationPanel` asks `ValidationPanel::refresh`, which calls
   `validateRunnableScene` and fills the validation table. Error diagnostics block Run; warnings do not.
4. **Run.** `Run` in the toolbar (Ctrl+R) or `File > Run` reaches `RunController::runScene`, which
   validates again and opens the review window **Run simulation**. Its button goes on to
   `DispatchController::prepareScene`, which validates, builds the infrastructure and the operations
   with `buildInfrastructureAndSignallingFromScene` and `buildOperationsFromScene`, and then to
   `RunController::startSimulation`. A `SimulationWorker` on its own thread calls
   `DispatchController::runSimulation`.
5. **Results.** When the worker finishes, `RunController::onSimulationFinished` copies what the views
   need into one `CompletedRunData` with `captureCompletedRun` and fills the **Run Results** dock.
   **Open result view...** in that dock opens the charts and tables, including the Delay analysis
   window. The output files are written to the [output folder](../guides/command-line.md#output-folder).
6. **Save.** `File > Save` goes through `SceneSession::saveSceneToCurrentDir` to `saveSceneBundle`
   or `saveScene` (`scene/SceneWriter`), and `SceneSession::finishSceneSave` marks the scene saved. A bundled case
   in the installation folder is never overwritten: Save leads to `Save As...`.

Messages go through the logging core in `util/Log`. Switch categories on with `EGTRAIN_LOG` and
read them in the **Log** dock (`View > Log`); see
[Log output](../guides/command-line.md#log-output).

## Which checks to run

Run the smallest set that covers the change. [Verification gates](testing.md#verification-gates)
is the full rule.

| Your change | Run |
| --- | --- |
| Documentation only | `python3 tools/docs/check_docs.py`; no build |
| Any C++ change | build, then `ctest --test-dir build -L unit -LE slow --output-on-failure -j 4` while you work and the whole `ctest --test-dir build --output-on-failure -j 4` before you push |
| UI or rendering | the whole suite, then `tools/e2e/visual_polish_smoke.sh` |
| Simulation, scene model, conversion or memory | the whole suite, which runs `tools/e2e/headless_smoke.py` and `tools/e2e/roundtrip_smoke.py` |
| Scene format | the same, and validate and export the committed scenes with `build/scene_tool` |

CI configures a Release build with `-DEGTRAIN_WARNINGS_AS_ERRORS=ON -DEGTRAIN_IPO=OFF` on all three systems, through
the presets `ci-macos`, `ci-linux` and `ci-windows` of `CMakePresets.json`; the warnings option is off by default. Add
it to your configure line to see the warnings that CI rejects. The `ci-windows` preset also uses Ninja with `cl` and
`-DEGTRAIN_NETWORK_TESTS=ON`; see [Configure options](ci-and-release.md#configure-options).

The two Python smokes read the application that `QEGTRAIN_APP` names, and otherwise the executable in
`build/` for the system: `build/QEGTRAIN.app/Contents/MacOS/QEGTRAIN` on macOS, `build/QEGTRAIN` on
Linux and `build/QEGTRAIN.exe` or `build/Release/QEGTRAIN.exe` on Windows. They stop and name the paths
they tried when none exists. `visual_polish_smoke.sh` reads the macOS path, so on Linux run it as
`QEGTRAIN_APP=$PWD/build/QEGTRAIN tools/e2e/visual_polish_smoke.sh`. `headless_smoke.py` runs the six committed cases one after the other; name case
numbers to run fewer, for example `python3 tools/e2e/headless_smoke.py 2`, because Copenhagen alone
takes minutes. On Linux and Windows CTest registers the tests that run there
([Platform coverage](testing.md#platform-coverage)).

Formatting and static checks run in CI too: `python3 tools/format.py` (`--fix` to apply) for the C++
sources, and `python3 tools/tidy.py` for the sources outside `simulation/`.

## A first change: one more test assertion

This changes no product behavior. It shows the build, test and failure loop on `util/TimeFormat`,
which has no Qt dependency and builds quickly. It starts from the finished build above.

1. Open `EGTRAIN/QEGTRAIN/tests/test_timeformat.cpp`. It is a Qt Test class
   ([Writing a test](testing.md#writing-a-test)). In `formatsSimTime_data()`, after the row
   `negative clamp`, add a row that is wrong on purpose:

   ```cpp
   QTest::newRow("two hours") << 7200LL << 0LL << "02:00:01";
   ```

2. Build and run only that test (with a multi-config generator, add `--config Release` and
   `-C Release` as above):

   ```bash
   cmake --build build --target test_timeformat
   ctest --test-dir build -R '^test_timeformat$' --output-on-failure
   ```

   CTest reports the test as failed, and the output has the line
   `FAIL!  : TestTimeFormat::formatsSimTime(two hours) Compared values are not the same`, followed by
   the actual and the expected text.

3. Correct the expected text to `"02:00:00"` and run the two commands again. The test passes.

4. Run the unit tests that finish quickly. `-j 4` runs four tests at a time. The set took about four
   minutes that way on one Apple silicon laptop and about one minute in one Linux container with 10 CPUs:

   ```bash
   ctest --test-dir build -L unit -LE slow --output-on-failure -j 4
   ```

5. Format the file if you keep the change (`python3 tools/format.py --fix`), or drop it with
   `git restore EGTRAIN/QEGTRAIN/tests/test_timeformat.cpp`.

For a validation rule, follow the same steps with `tests/test_scenevalidator.cpp`: build a model that
breaks the rule and check the diagnostic with `hasCodeAndSeverity`. A new test needs an `egtrain_add_test`
entry with its labels in `EGTRAIN/QEGTRAIN/tests/CMakeLists.txt`; [Tests](coding-guidelines.md#tests) shows how.

## Where output goes

[Where test and run output goes](smoke-tests.md#where-test-and-run-output-goes) lists where a
run, CTest and each smoke write their files.

## Contributing

What the repository itself shows:

- Pull requests go to `main`. CI builds and runs the CTest suite on macOS, Windows and Linux without
  the tests labelled `network`, which run on Windows only, unless every changed file is documentation. The format and tidy checks run when C++ sources change.
  [CI and release branches](ci-and-release.md#ci-and-release-branches) lists the workflows, and
  [Required checks on `main`](ci-and-release.md#required-checks-on-main) the checks that must pass.
- Code follows [Coding guidelines](coding-guidelines.md). `.clang-format` and `.editorconfig` set the
  format: tabs, braces attached, no column limit.
- Documentation changes pass `python3 tools/docs/check_docs.py`, and every page is in the table of contents,
  [docs/index.md](../index.md).

## Related documents

- [Build from source](build.md), [Testing](testing.md), [Smoke tests](smoke-tests.md), [Simulation characterization tests](characterization-tests.md) and [CI and release](ci-and-release.md).
- [Source layout](../architecture/source-layout.md) and [Coding guidelines](coding-guidelines.md).
- [Release testing checklist](release-testing-checklist.md) and [Code signing](code-signing.md).
- [Playback profiling](playback-profiling.md) and [Peak-memory baselines](memory-baselines.md).
- [Glossary of application terms](../guides/glossary.md).
