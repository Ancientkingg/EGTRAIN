# Getting started as a developer

This page takes you from a fresh clone to a built application, a first run, a changed test and the
checks a pull request has to pass. It points to the longer guides for details. EGTRAIN is a Qt 5 and
C++17 desktop application for railway micro-simulation, built with CMake.

The commands were last run from a fresh clone on macOS (Apple silicon) on 2026-10-17. On Linux,
configure, build, CTest, the headless run, both smokes and the test walkthrough below were run in a
clean Ubuntu 24.04 arm64 container on 2026-10-17 at `dbaadd94`. One test, `test_measure_peak_rss`,
failed there because the first process of the container did not reap orphaned children; the test
counts such a zombie as exited. The Windows commands on this page were not run. CI installs the same packages as
[Build and test](build-and-test.md#configure) lists, but it configures with more options; see
[Which checks to run](#which-checks-to-run). The Visual Studio generator path in the build guide
is not what CI uses and was not checked for this page.

## What you need

| Tool | Version | Notes |
| --- | --- | --- |
| Git | any recent release | |
| CMake | 3.16 or newer | `CMakeLists.txt` at the repository root |
| C++ compiler | C++17 | Apple Clang on macOS, MSVC 2019 or newer on Windows (x64 only), GCC on Ubuntu |
| Qt 5 | Core, Gui, Widgets, Charts, Svg and Network | CI uses Homebrew `qt@5`, Qt 5.15.2 `msvc2019_64` and the Ubuntu packages |
| ZeroMQ, cppzmq, nlohmann-json | any release that CMake finds | `libzmq3-dev cppzmq-dev nlohmann-json3-dev` on Ubuntu, vcpkg on Windows |
| Python | 3.9 or newer | CMake looks for it when tests are on; CI uses 3.12 |
| clang-format | 23.1.3 exactly | Only to format C++ sources: `python3 -m pip install clang-format==23.1.3` |
| clang-tidy | 23.1 | Only for [`tools/tidy.py`](coding-guidelines.md#static-checks) |

[Requirements](build-and-test.md#requirements) lists the supported toolchains and the exact install
commands for each system.

## Clone, configure and build

Clone the repository and change into it:

```bash
git clone https://github.com/Ancientkingg/EGTRAIN.git
cd EGTRAIN
```

On macOS with Homebrew:

```bash
brew install qt@5 zeromq cppzmq nlohmann-json
cmake -S . -B build -DEGTRAIN_BUILD_TESTS=ON -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt@5
cmake --build build --parallel 4
```

Homebrew deprecated `qt@5` on 2026-05-19 and disables it on 2027-05-19
([formula](https://formulae.brew.sh/formula/qt@5)). After that date, install Qt 5.15 another way and point
`CMAKE_PREFIX_PATH` at it, for example Qt 5.15.2 from the Qt online installer or from
[aqtinstall](https://aqtinstall.readthedocs.io/en/latest/getting_started.html). On an Intel Mac:

```bash
aqt install-qt mac desktop 5.15.2 clang_64 -m qtcharts -O "$HOME/Qt"
cmake -S . -B build -DEGTRAIN_BUILD_TESTS=ON -DCMAKE_PREFIX_PATH="$HOME/Qt/5.15.2/clang_64"
```

The macOS binaries of Qt 5.15.2 are for Intel only. On Apple silicon they need an Intel build with
`-DCMAKE_OSX_ARCHITECTURES=x86_64` and an Intel build of ZeroMQ, and run under Rosetta;
building Qt 5.15 from source is the other way. The [Qt 6 migration study](qt6-migration-study.md) describes the
move to Qt 6, which removes the need for `qt@5`.

On Ubuntu and Windows, take the install and configure lines from
[Configure](build-and-test.md#configure); the Ubuntu line installs `git` and `python3` too. The build, test and run commands below use the macOS and
Linux form. With a multi-config generator such as Visual Studio, add `--config Release` to
`cmake --build` and `-C Release` to `ctest`, and run the executable from
[Run a local build](build-and-test.md#run-a-local-build).

Without `--parallel`, `cmake --build` uses one job with the Makefile generator, which takes a long
time; `CMAKE_BUILD_PARALLEL_LEVEL` sets the number of jobs as well. A first full build with tests
and four jobs took about 14 minutes on one Apple silicon laptop and about 7 minutes in one Linux
container with 10 CPUs; it will differ on other machines.

A Release build, the default, links with link-time optimization. `-DEGTRAIN_IPO=OFF` at configure
time makes the links shorter.

The build makes three programs and, with `EGTRAIN_BUILD_TESTS=ON`, one executable for each test:

| Program | What it is | macOS path | Other systems |
| --- | --- | --- | --- |
| `QEGTRAIN` | The application | `build/QEGTRAIN.app/Contents/MacOS/QEGTRAIN` | [Run a local build](build-and-test.md#run-a-local-build) |
| `scene_tool` | Validates, packs, unpacks, imports and exports scenes without the window | `build/scene_tool` | `build/scene_tool` or `build/Release/scene_tool.exe` |
| `egtrain_update_helper` | Helper of the self-updater of release packages | `build/egtrain_update_helper` | |

If configure or the build stops, see [Common setup failures](build-and-test.md#common-setup-failures).

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
a single-config one; see [Run a local build](build-and-test.md#run-a-local-build).

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
nothing to a terminal; see [Windows headless runs](build-and-test.md#windows-headless-runs).
The bundled cases and their `-n` numbers are in [Case study numbers](../guides/command-line.md#case-study-numbers).

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
| Results, charts, analysis windows, CSV export | `diagrams/` (`CompletedRunCapture`, `RunResults`, `RunExports`) |
| The network canvas | `graphics/` and `graphics/items/` |
| Dock widgets, dialogs, small controls | `widgets/` |
| Logging, time and number formatting | `util/` |
| Live stream and legacy XML channels | `io/` |
| Tests | `tests/` (C++), `tools/e2e/` (scripts) |
| Committed case studies | `Scenes/` |
| Release, memory and performance scripts | `tools/release/`, `tools/memory/`, `tools/performance/` |

## From open to saved

This is the path of one case through the code, in the order a user takes it. It is the quickest way
into `MainWindow.cpp`. [Scene editor flow](../architecture/scene-editor-flow.md) describes the same
path from the user's side, and [Scene model](../architecture/scene-model.md) describes the data.

1. **Open.** `File > Open Case Study...` (a `.egscene` bundle), `File > Open Scene Folder...` or the
   case chooser lead to `SceneSession::openSceneDirectory`. It probes the scene format, and the window
   loads the scene with `loadScenePath` into a `SceneModel` and hands it to its `SceneDocument`.
2. **Edit.** An editor panel changes the model in place and calls `SceneDocument::markChanged`.
   `MainWindow::sceneDocumentChanged` records the step for Undo and Redo, discards the results of an
   earlier run and schedules the recovery copy.
3. **Validate.** `MainWindow::refreshValidationPanel` asks `ValidationPanel::refresh`, which calls
   `validateRunnableScene` and fills the validation table. Error diagnostics block Run; warnings do not.
4. **Run.** `Run` in the toolbar (Ctrl+R) or `File > Run Scene` reaches `RunController::runScene`, which
   validates again and opens the review window **Run simulation**. Its button goes on to
   `DispatchController::prepareScene`, which validates, builds the infrastructure and the operations
   with `buildInfrastructureAndSignallingFromScene` and `buildOperationsFromScene`, and then to
   `RunController::startSimulation`. A `SimulationWorker` on its own thread calls
   `DispatchController::runSimulation`.
5. **Results.** When the worker finishes, `RunController::onSimulationFinished` copies what the views
   need into one `CompletedRunData` with `captureCompletedRun` and fills the **Run Results** dock.
   **Open result view...** in that dock opens the charts and tables, including the Delay analysis
   window. The output files are written to the [output folder](../guides/command-line.md#output-folder).
6. **Save.** `File > Save Scene` goes through `MainWindow::saveSceneToCurrentDir` to `saveSceneBundle`
   or `saveScene` (`scene/SceneWriter`), and `finishSceneSave` marks the scene saved. A bundled case
   in the installation folder is never overwritten: Save leads to `Save Case Study As...`.

Messages go through the logging core in `util/Log`. Switch categories on with `EGTRAIN_LOG` and
read them in the **Console Log** dock (`View > Console Log`); see
[Log output](../guides/command-line.md#log-output).

## Which checks to run

Run the smallest set that covers the change. [Verification gates](build-and-test.md#verification-gates)
is the full rule.

| Your change | Run |
| --- | --- |
| Documentation only | `python3 tools/docs/check_docs.py`; no build |
| Any C++ change | build, then `ctest --test-dir build -L unit -LE slow --output-on-failure -j 4` while you work and the whole `ctest --test-dir build --output-on-failure -j 4` before you push |
| UI or rendering | the whole suite, then `tools/e2e/visual_polish_smoke.sh` |
| Simulation, scene model, conversion or memory | the whole suite, then `python3 tools/e2e/headless_smoke.py` and `python3 tools/e2e/roundtrip_smoke.py` |
| Scene format | the same, and validate and export the committed scenes with `build/scene_tool` |

CI configures with `-DEGTRAIN_WARNINGS_AS_ERRORS=ON -DEGTRAIN_IPO=OFF` on all three systems; the
warnings option is off by default. Add it to your configure line to see the warnings that CI rejects.
The Windows leg also uses Ninja with `cl`, `-DCMAKE_BUILD_TYPE=Release` and
`-DEGTRAIN_NETWORK_TESTS=ON`; see `.github/workflows/cmake.yml`.

The two Python smokes read the application that `QEGTRAIN_APP` names, and otherwise the executable in
`build/` for the system: `build/QEGTRAIN.app/Contents/MacOS/QEGTRAIN` on macOS, `build/QEGTRAIN` on
Linux and `build/QEGTRAIN.exe` or `build/Release/QEGTRAIN.exe` on Windows. They stop and name the paths
they tried when none exists. `visual_polish_smoke.sh` reads the macOS path, or the path in
`QEGTRAIN_APP`. `headless_smoke.py` runs the seven committed cases one after the other; name case
numbers to run fewer, for example `python3 tools/e2e/headless_smoke.py 2`, because Copenhagen alone
takes minutes. On Linux and Windows CTest registers the tests that run there
([Platform coverage](build-and-test.md#platform-coverage)).

Formatting and static checks run in CI too: `python3 tools/format.py` (`--fix` to apply) for the C++
sources, and `python3 tools/tidy.py` for the sources outside `simulation/`.

## A first change: one more test assertion

This changes no product behavior. It shows the build, test and failure loop on `util/TimeFormat`,
which has no Qt dependency and builds quickly. It starts from the finished build above.

1. Open `EGTRAIN/QEGTRAIN/tests/test_timeformat.cpp`. Tests here are plain programs with a `main()` and a
   local `expect(bool, message)`. After the line `formatSimTime negative clamp`, add an assertion that
   is wrong on purpose:

   ```cpp
   ok &= expect(formatSimTime(7200) == "02:00:01", "formatSimTime two hours");
   ```

2. Build and run only that test (with a multi-config generator, add `--config Release` and
   `-C Release` as above):

   ```bash
   cmake --build build --target test_timeformat
   ctest --test-dir build -R '^test_timeformat$' --output-on-failure
   ```

   CTest reports the test as failed and the output has the line `failed: formatSimTime two hours`.

3. Correct the expected text to `"02:00:00"` and run the two commands again. The test passes.

4. Run the unit tests that finish quickly. `-j 4` runs four tests at a time. The set took about four
   minutes that way on one Apple silicon laptop and about one minute in one Linux container with 10 CPUs:

   ```bash
   ctest --test-dir build -L unit -LE slow --output-on-failure -j 4
   ```

5. Format the file if you keep the change (`python3 tools/format.py --fix`), or drop it with
   `git restore EGTRAIN/QEGTRAIN/tests/test_timeformat.cpp`.

For a validation rule, follow the same steps with `tests/test_scenevalidator.cpp`: build a model that
breaks the rule and check the diagnostic with `hasCodeAndSeverity`. A new test needs an entry in the
root `CMakeLists.txt` and a label; [Tests](coding-guidelines.md#tests) shows how.

## Where output goes

[Where test and run output goes](build-and-test.md#where-test-and-run-output-goes) lists where a
run, CTest and each smoke write their files.

## Contributing

What the repository itself shows:

- Pull requests go to `main`. CI builds and runs the CTest suite on macOS, Windows and Linux without
  the tests labelled `network`, which run on Windows only, unless every changed file is documentation. The format and tidy checks run when C++ sources change.
  [CI and release branches](build-and-test.md#ci-and-release-branches) lists the workflows, and
  [Required checks on `main`](build-and-test.md#required-checks-on-main) the checks that must pass.
- Code follows [Coding guidelines](coding-guidelines.md). `.clang-format` and `.editorconfig` set the
  format: tabs, braces attached, no column limit.
- Documentation changes pass `python3 tools/docs/check_docs.py`, and every guide is reachable from
  [docs/README.md](../README.md).

## Related documents

- [Build and test](build-and-test.md): configure recipes, test labels, smokes, CI.
- [Source layout](../architecture/source-layout.md) and [Coding guidelines](coding-guidelines.md).
- [Release testing checklist](release-testing-checklist.md) and [Code signing](code-signing.md).
- [Playback profiling](playback-profiling.md) and [Peak-memory baselines](memory-baselines.md).
- [Glossary of application terms](../guides/glossary.md).
