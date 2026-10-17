# Coding guidelines

This guide records the conventions that new code follows. The newer modules (`scene/`,
`update/`, `diagrams/`, `graphics/`, `widgets/`, `telemetry/`) set the style.
`simulation/` is legacy code with different naming, fixed-size global arrays and
`std::list` containers; new code follows the newer modules, including when it is added to
`simulation/`. These rules do not ask for a rename of legacy code.

Formatting follows `.clang-format` and `.editorconfig` in the repository root.
`python3 tools/format.py --fix` formats every C++ source under `EGTRAIN/QEGTRAIN`, and
`python3 tools/format.py` checks them and lists the files that differ. The pull request
checks run the same check. The script needs clang-format 23.1.3, because other releases
format some constructs differently: `python3 -m pip install clang-format==23.1.3`
installs it, and the environment variable `CLANG_FORMAT` names the executable when it is
not the `clang-format` on `PATH`. The vendored code in `io/third_party/` is listed in
`.clang-format-ignore` and is not reformatted.

The formatter keeps the line breaks of the author and sets no column limit. It keeps a
break before a binary operator, and a break inside an argument only when that argument
starts on a line of its own. A statement that is wrapped elsewhere comes out as one long
line, so wrap it at one of those places.

`.git-blame-ignore-revs` lists the commits that only changed the format.
`git config blame.ignoreRevsFile .git-blame-ignore-revs` makes `git blame` skip them, and
the blame view of GitHub skips them without a setting.

## Static checks

`python3 tools/tidy.py` runs clang-tidy on the sources of the modules outside `simulation/`
and lists the findings; it exits with 1 on any finding. The pull request checks run the
same script. It configures a Ninja build in `build_tidy/` (`--build-dir` changes it), builds
the files that Qt generates (moc, uic and resources), and analyses the sources of the build
in parallel. It needs CMake, Ninja, Qt 5 and clang-tidy 23.1, the release that the findings
are agreed for: `brew install llvm` on macOS, or the `llvm.sh` script of apt.llvm.org on
Ubuntu (PyPI has no clang-tidy 23). The environment variable `CLANG_TIDY` names the
executable when it is not the `clang-tidy` on PATH. The script adds the macOS SDK path
itself.

- `python3 tools/tidy.py` analyses every source.
- `python3 tools/tidy.py FILE...` analyses the named sources.
- `python3 tools/tidy.py --changed-from REV` analyses the sources that changed since `REV`
  and the sources that include a changed header. The pull request check passes the base
  of the pull request. It analyses every source when `.clang-tidy`, `tools/tidy.py`, a
  `CMakeLists.txt`, a `.ui` or `.qrc` file, or the workflow changed. The nightly and the
  manual run of the Tidy workflow analyse every source.
- `--no-build` reuses the configured build directory, which is faster when only a source
  changed.
- `-j N` sets the number of parallel clang-tidy processes (default: the number of CPUs).

The checks are selected in `.clang-tidy`, and they apply to every analysed module:
`app/`, `diagrams/`, `editors/`, `graphics/`, `io/`, `recovery/`, `scene/`, `telemetry/`,
`update/`, `util/`, `widgets/` and `tests/`. Two places differ. `tests/.clang-tidy`
inherits them and switches off the performance checks and a few others for the tests, with
the reason of each next to it. `simulation/` is not analysed yet, and its headers are not
in the header filter, so a finding in one of them does not fail a source that includes it.
`io/third_party/` has its own `.clang-tidy` that switches the analysis off.

A finding is fixed in the code. When the check is wrong for one place, the line carries
`// NOLINT(check-name): reason`. These checks are switched off for every module because
their findings are style or noise that a rewrite would not repay, or because the check
cannot see how Qt owns objects.

| Check | Why it is off |
| --- | --- |
| `bugprone-signed-bitwise` | Qt flags, hashes and masks computed in `int` |
| `bugprone-exception-escape` | `main`, destructors and lambdas of slots; the application does not throw across them |
| `bugprone-throwing-static-initialization` | Statics that hold a `std::string` or a mutex |
| `bugprone-implicit-widening-of-multiplication-result` | Products of small values, such as milliseconds per second |
| `bugprone-unchecked-string-to-number-conversion` | `atoi` and `atof` on command line values in `app/main.cpp` |
| `bugprone-nondeterministic-pointer-iteration-order` | Vectors of pointers that are sorted by a field, not by address |
| `bugprone-derived-method-shadowing-base-method` | `NetworkView::centerOn` hides the Qt method on purpose |
| `bugprone-misplaced-widening-cast` | `static_cast<std::size_t>(index + 1)` on a checked index |
| `clang-analyzer-cplusplus.NewDeleteLeaks` | A Qt parent owns the object that `new` creates |
| `clang-analyzer-optin.core.EnumCastOutOfRange` | In `zmq.hpp`, which is not ours |
| `performance-enum-size` | Style |
| `performance-avoid-endl` | The log wants the flush |
| `performance-inefficient-vector-operation` | A `reserve` that would save little |
| `performance-inefficient-string-concatenation` | A few temporaries in code that is not hot |
| `performance-faster-string-find`, `performance-prefer-single-char-overloads` | Micro-optimisations |

Two checks are not selected, because their findings are not worth fixing. Counts are from
clang-tidy 23.1.3 over all analysed modules.

| Check | Findings | Why it is not selected |
| --- | --- | --- |
| `readability-implicit-bool-conversion` | 2269, of which 1407 in `tests/` | Almost all are a pointer or an integer used as a condition (`if (ptr)`, `!ptr`), the idiom of the Qt code and of this code. With those conditions allowed, 455 remain (391 in `tests/`), mostly the same idiom inside a larger expression such as `a && !m_worker`. `scene/`, `io/` and `util/` have 44, all of the first kind |
| `cppcoreguidelines-owning-memory` | 918, of which 183 in `tests/` | The check follows `gsl::owner`, and nearly every finding is `new` for a widget, layout, action or thread that a Qt parent owns. `scene/`, `io/` and `util/`, which do not use Qt ownership, have none |

## Warnings

`egtrain_target_warnings(<target> [STRICT])` in the root `CMakeLists.txt` adds `-Wall -Wextra`
(`/W4` with MSVC) to the C++ sources of one target. The flags are private to the target, so a
target that links a library does not get them.

- Strict targets build without a warning: `egtrain_util`, `egtrain_scene`, `egtrain_live`, `egtrain_http`, `egtrain_update`,
  `egtrain_sceneopen`, `egtrain_openrelay`, `egtrain_recovery`, `egtrain_graphics`, `egtrain_widgets`, the `egtrain_telemetry` libraries, `egtrain_diagrams`,
  `scene_tool`, `egtrain_update_helper` and the tests that use only these libraries. When
  `EGTRAIN_WARNINGS_AS_ERRORS` is ON they also get `-Werror` (`/WX` with MSVC), so a new
  warning fails the build.
- `egtrain_dispatch` has the flags only. Its own sources are clean, and the warnings in its
  build log come from the simulation headers.
- These have no flags: the vendored `egtrain_pugixml` and `egtrain_miniz`, the legacy
  `egtrain_sim` and `egtrain_rttp`, `QEGTRAIN`, and the test executables that use the
  simulation. A comment next to the calls gives the number of warnings.
- `update/SelfUpdater.cpp`, `update/UpdateNotices.cpp`, `app/main.cpp` and
  `telemetry/TelemetryConsentDialog.cpp` build inside `QEGTRAIN`, so they get no flags yet.
  `update/SelfUpdater.cpp` is the exception in the strict `test_selfupdater`, which compiles it with the flags.

To give a new target the flags, call `egtrain_target_warnings(<target> STRICT)` after its
`add_library` or `add_executable`, or add a test to the list of strict tests in the test block,
and fix its warnings first. A source in a strict target that needs `simulation/RollingStock.h`
or `simulation/Signalling.h` includes `diagrams/SimulationHeaders.h` instead, because those
headers still give warnings and that header keeps them out of the build. A function that a
platform does not use, such as the update stagers in `update/UpdatePreparation.cpp`, is marked
`[[maybe_unused]]`.

To treat these warnings as errors in a local build, configure with the option:

```bash
cmake -S . -B build -DEGTRAIN_BUILD_TESTS=ON -DEGTRAIN_WARNINGS_AS_ERRORS=ON
```

The option is OFF by default, so a newer compiler cannot break a local build with a warning
that the code did not give before. The pull request checks configure with the option ON on
macOS, Linux and Windows.

The three compilers do not report the same things. MSVC `/W4` also reports a local name that
hides a class member, a global variable or a local of an enclosing scope (C4456 to C4459), and
a 32-bit shift that is widened to 64 bits (C4334). Clang and GCC accept both with
`-Wall -Wextra`, so such code passes a local build on macOS or Linux and fails the Windows
check. A target with the flags is compiled with `_CRT_SECURE_NO_WARNINGS` on Windows, because
MSVC otherwise reports standard functions such as `std::getenv` as deprecated.

## File layout

[Source layout](../architecture/source-layout.md) describes the folders and the include
convention. Use this table to decide where a new file goes. Paths are relative to
`EGTRAIN/QEGTRAIN/` unless noted.

| Folder | Put here |
| --- | --- |
| `app/` | Entry point, main window and the controller that prepares and runs a scene |
| `simulation/` | The simulation engine and the builders that fill its runtime data from a `SceneModel` (legacy style) |
| `scene/` | The canonical scene model: load, validate, write, import, export, bundle and migrate |
| `graphics/` | The network canvas and view; `graphics/items/` holds the `QGraphicsItem` subclasses |
| `widgets/` | Dock widgets, dialogs and small controls |
| `diagrams/` | Result windows, charts and the run-result data they show |
| `io/` | Exchange formats for external tools; vendored libraries in `io/third_party/` |
| `util/` | Helpers shared by several folders: formatting, CSV output, versions, profiling |
| `update/` | Release check, package download, self-update and the `egtrain_update_helper` executable |
| `telemetry/` | Consent, queue and sender for usage and diagnostics events, see the [wire contract](../telemetry/README.md) |
| `recovery/` | Recovery copies of a scene with unsaved edits, kept below a folder that the caller names, and the controller that writes them in the background |
| `tests/` | C++ test executables, one per file, with data in `tests/fixtures/`. The folder `tests/e2e/` is the runner of the scripted GUI scenarios, compiled into `QEGTRAIN` itself and not a test executable |
| `tools/` | At the repository root: Python and shell tests and helpers (`e2e/`, `golden_master/`, `memory/`, `performance/`, `release/`) |

- Add a new `.cpp` to the library that holds its folder ([Libraries](../architecture/source-layout.md#libraries)),
  or to `EGTRAIN_SOURCES` when only the application uses it. Add a new `.h` to `EGTRAIN_HEADERS`
  unless it belongs to a library source. Both lists are in the root `CMakeLists.txt`.
- Include project headers with quotes and a path from `EGTRAIN/QEGTRAIN/`, for example
  `#include "scene/SceneModel.h"`. Qt and standard headers use angle brackets.
- `scene/` and `util/` contain no Qt types, and `scene_tool` links no Qt. Code that needs
  Qt goes elsewhere and converts at the boundary, for example with `QString::fromStdString`.
- New headers use `.h` and `#pragma once`. Guard styles are mixed today (`#ifndef` in
  `scene/`, `graphics/`, `diagrams/` and most of `update/`; `#pragma once` in `telemetry/`,
  `update/HttpTransport.h` and part of `widgets/`), and `.hpp` appears only in older `util/`
  files and the vendored code.

## Naming

- Files: PascalCase, named after the main type or group of functions
  (`scene/SceneValidator.h`, `graphics/items/NodeItem.cpp`). The lower-case exceptions are
  `main.cpp`, `util/portability.h` and `util/timeutil.hpp`.
- Types: PascalCase (`UpdateChecker`, `SceneDiagnostic`). New enumerations are
  `enum class` with PascalCase values (`SceneSeverity::Warning`). Plain enums appear where
  Qt expects integers, such as `enum { Type = UserType + N }` in `graphics/items/`.
- Functions, methods, variables and parameters: camelCase (`loadScene`,
  `classifyTrackSpeed`). Accessors have no `get` prefix (`stationName()`), setters start
  with `set`, and boolean queries start with `is` or `has` (`isChecking()`).
- Class data members: `m_` prefix (`m_transport`, `m_reply` in `update/UpdateChecker.h`). A
  few classes use a trailing underscore (`app/GuiReplayHistory.h`); new code uses `m_`.
  Public fields of plain data structs have no prefix (`SceneDiagnostic::itemId`).
- Constants are `constexpr` with a `k` prefix: `kUpdateCheckTimeoutMs` in an anonymous
  namespace in `update/UpdateChecker.cpp`, `kCurrentSceneSchemaVersion` as `inline constexpr`
  in `scene/SceneModel.h`. Helpers used by one `.cpp` file also go in an anonymous namespace.
- Mark overriding virtual functions `override`, as `graphics/items/StationOverlayItem.h`
  does. `modernize-use-override` checks it.
- `simulation/` mixes snake_case, Mixed_Case and camelCase (`signalling_block_sections`,
  `Signalling_Level`, `numTrackLines`). Do not copy those forms.

## Ownership

New code adds no owning raw pointers. Choose the owner by what the object is.

- QObjects, widgets and dialogs: the parent owns its children. Pass the parent when you
  create a child and keep only a non-owning pointer (`m_toggle = new QToolButton(this)` in
  `widgets/NetworkLegendWidget.cpp`, `new QThread(this)` in `update/SelfUpdater.cpp`). A
  constructor that can have a parent takes `QObject* parent = nullptr` and passes it to
  the base class (`UpdateChecker`). Use `QPointer` for a pointer whose target another owner
  can delete (`m_reply` in `update/UpdateChecker.h`) and `deleteLater()` for an object
  without a parent.
- Graphics items: the scene owns an item after `scene->addItem(item)`, and a child item
  gets its parent item when it is created (`graphics/items/SignalItem.cpp`). Pointers to
  scene items held elsewhere only observe them: `NetworkPresenter::clear` calls
  `scene->clear()`, which deletes the items, then clears the lists that point at them
  (`graphics/NetworkPresenter.cpp`, documented in `graphics/NetworkPresenter.h`).
- Everything else: `std::vector` for collections and arrays, `std::unique_ptr` for one owned
  object that is not a QObject. `SceneModel` holds its entity lists in `std::vector` members,
  `widgets/ConsoleWidget.h` owns a `std::unique_ptr<ConsoleStreambuf>`, and
  `telemetry/TelemetryQueue.h` owns a `std::unique_ptr<QLockFile>`.
- `simulation/` uses `extern` globals. Do not add more.
  State that a run builds up and the next run must not see is a member of `SimulationRun`
  (`simulation/SimulationRun.h`). `SimulationRun::reset()` clears every member except the
  infrastructure in place. The old global names of the members that moved there, such as
  `BlocksOccupied` and `ETCS_MA`, are references to them. A new member needs a line in `reset()`.
  The routines of `simulation/` take the run they work on as a `SimulationRun&` parameter, and the
  member functions of a `Train` reach it through `Train::run_`, which `buildOperationsFromScene`
  sets. They do not use the old names, which only the tests still use. The window and `main.cpp` read the run
  through `DispatchController::run()`, and `captureCompletedRun` takes it as a parameter. The canvas takes
  `mutableRun()` because it writes its layout into the tracks, stations and connections.
  `SimulationRun::settings` is the copy of `initial_variables` that `DispatchController::prepareScene`
  takes once the scene is built. `DispatchController` and the routines of `simulation/` read the
  buffer time, the recovery time, the output folder and the other settings of a run from the copy,
  so a change to `initial_variables` afterwards does not reach the run. The builders and `prepareScene`
  read `initial_variables` because they make the copy. `timestep` and `signalCode1` to
  `signalCode3` are `constexpr` values in `simulation/Infrastructure.h`.
  `DispatchController::prepareScene` builds the scene into a staged `SimulationRun` and puts it in the
  place of the run of the controller with `SimulationRun::adopt()`, which points the trains at the
  run and builds the indexes again; a member that points at another member of the run needs a line
  there. A scene that fails leaves the controller with a new run. While the worker thread steps a run
  the window sees it only through the snapshots, which `buildGuiSimulationSnapshot(const SimulationRun&, int)`
  makes. After the worker has joined, `DispatchController::run()` gives the results as a `const SimulationRun&`.
  The infrastructure and the routes are `SimulationRun::infra`, a `RunInfrastructure`: the
  tracks, connections and stations (`blockSets`, `connections` and `StationArray`, vectors with
  a fixed number of elements that nothing resizes), the block sections, the detection sections,
  the single-track data, with the zones that `singleTrackZone` derives from it, and `train_route`.
  The scene builder replaces them and the previous ones
  stay until then, so `resetInfrastructure()` is a separate call, and a build that fails puts the
  earlier infrastructure back as it was. The canvas keeps `Node*`, `Arc*` and `Connections*` into
  the arrays, so `MainWindow` removes its items before a reset or a build. A new member of the
  infrastructure needs a line in `RunInfrastructure::reset()`.
  `regional_train` is a reference to the `std::vector<Regional>` of the run, created with the
  number of trains of the scene (declared in `simulation/RollingStock.h`); `Max_N_Reg` is
  only the limit that validation and the editor enforce on that number. A `Train` keeps
  the stops of its service in a `std::vector<Node>`, and `regional_train` is created once
  per build and not grown while the trains are live. A `Section` keeps the nodes of its block
  section in a `std::shared_ptr<const std::vector<Node>>` that the section and its copies,
  including those in the routes, share, and the last of them frees it. A reversed route
  section holds no list. A `Route` holds the sections
  of its route in a `std::vector<Section>` with exactly `N_Block_Sections` elements, set when
  the route is built; code that takes a `Section*` and a count gets `data()` and that count.

`tools/memory/ownership_inventory.py` lists owning allocations, Qt parent allocations,
arrays of 100 or more elements and raw pointers, and its `CONFIRMED_OBSERVERS` table names
the pointers known to be non-owning. `test_ownership_inventory` runs the scanner on the real
tree and a fixture, and `test_raii_contract` checks that the delay statistics in
`simulation/Simulation.cpp` use `std::vector` instead of `new double[]`, that the retired
`setupEgtrain` and `prepareSimulation` stay out of `app/DispatchController.cpp` and that
`runSimulation` writes each train trajectory file once, as its last stage, and leaves
`BlockingTimes.txt` to `ComputeBlockingTimesInMixedSignallingForAllTrains`. When
`test_raii_contract` fails, remove the raw allocation, the restored path or the repeated
write instead of editing the test; when `test_ownership_inventory` fails after a pointer in
`CONFIRMED_OBSERVERS` was renamed or removed, update the table and the test in the same change.

## Error reporting

Scene loading, validation, import, export, bundles, migration and the native builders report
problems as `SceneDiagnostic` values (`scene/SceneDiagnostic.h`). A new check does the same:
it adds a diagnostic to the vector its function returns or to the `diagnostics` member of
the result struct (`SceneLoadResult`), and it does not throw to the caller, print or log. An
exception from a library is caught where it happens: `loadScene` in `scene/SceneModel.cpp`
turns a JSON parse failure into `scene.json.parse` with its local `addError` helper. Put a
new check in the layer it belongs to
([Validation layers](../architecture/scene-model.md#validation-layers)). Each diagnostic
carries these fields:

- `code`: a stable id, lower case with dots, `scene.<area>.<problem>`, for example
  `scene.ref.unresolved`, `scene.id.duplicate` or `scene.time.order`. Import, export, bundle,
  migration, compatibility and runtime-builder checks use the areas `import`, `export`,
  `bundle`, `migration`, `compatibility` and `native`. Reuse an existing code for the same
  kind of problem; tests match on codes.
- `severity`: `Error` fails the operation (`hasErrors`) and disables Run, `Warning` marks
  data that still runs (historical schedules), and `Info` notes what an import or export did.
- `message` is one sentence that names the item. `file`, `itemType`, `itemId` and `path`
  locate it: the scene file, the kind and id of the item, and the place inside the file
  (`nodes[0].track`). `relatedId` is the other item involved, such as the unresolved target.
  `suggestedFix` is the smallest fix, when one is known.

`DiagnosticBuilder` in `scene/SceneValidator.cpp` takes these fields in that order, with the
severity chosen by `error` or `warning`:

```cpp
if (!hasId(trackIds, node.trackId)) {
	diagnostics.error("scene.ref.unresolved", "Node refers to unknown track", "infrastructure.json",
			"node", node.id, path + ".track", node.trackId,
			"Add track " + node.trackId + " or reference an existing track");
}
```

## Logging

Console output comes from the logging core in `util/Log.h` and from older code that writes
to the console directly.

- `util/Log.h` is a categorised logging core without Qt. `EG_LOG(category, level) << ...`
  writes one message: the category is one of `SimSetup`, `SimStep`, `SimEvents`, `SimPax`,
  `IoRailml`, `IoZmq`, `AppRun`, `AppUi` and `Update` (named `sim.setup`, `sim.step`,
  `sim.events`, `sim.pax`, `io.railml`, `io.zmq`, `app.run`, `app.ui` and `update`), and the
  level is `Debug`, `Info`, `Warning` or `Error`. The text is delivered as streamed, so the
  caller writes the newline. A disabled message evaluates none of its arguments. Every pair is
  enabled by default except `sim.step` at `debug`.
- `eglog::configure(rules)` switches pairs on and off with the syntax of `QT_LOGGING_RULES`:
  rules separated by `;` or a newline, each `pattern=true` or `pattern=false`, where the
  pattern is a category name or a name with a `*` at the start or the end (`sim.*`, `*.run`,
  `*step*`), with an optional level suffix (`sim.setup.warning=false`). A later rule wins, a
  malformed rule is skipped and makes the function return false, and `configure("")` restores
  the defaults. `eglog::configureFromEnvironment()` applies the variable `EGTRAIN_LOG`. The
  variable has no effect unless the program calls that function, and the caller reports a
  false result.
- The default sink writes the text unchanged to `std::cout`, for every category and level.
  `eglog::setSink` replaces it; a sink must not call the logging core.
- `installConsoleLogging()` (`widgets/ConsoleWidget.h`) is called at the start of `main()`, right after the startup timer starts. It installs
  the sink `consoleLogSink` and the Qt message handler `qtMessageToLog`, applies `EGTRAIN_LOG`
  with `eglog::configureFromEnvironment()` and opens the file named by `EGTRAIN_LOG_FILE`. It
  returns a warning for `main()` to print on `std::cerr` when a rule is malformed or the file
  cannot be opened. The sink writes the text unchanged, with no prefix, to the same streams as
  the default sink, except that `app.ui` goes to `std::cerr` while the Console Log dock is not
  installed, because Qt's own handler writes there. The dock shows `level category: ` in front of
  each message that comes through the core, taken from the values the core passes; text written
  to `std::cout` or `std::cerr` directly shows as written. The log file gets the same lines
  (`warning app.ui: text`), each message on a line of its own, appended, with no size limit.
- Every Qt message (`qDebug()`, `qInfo()`, `qWarning()`, `qCritical()`, `qFatal()`) reaches the
  core as `app.ui` at the matching level, with the text that Qt's own handler prints, so
  `EGTRAIN_LOG` switches Qt messages on and off per level and `QT_MESSAGE_PATTERN` still
  applies. A `qFatal()` message is never filtered. The handler writes to the console streams
  only, not to the system log.
- `simulation/`, `io/` and `app/DispatchController.cpp` write through `EG_LOG`, except for the
  marker line `QEGTRAIN_NATIVE_DETAIL`. `BlockingTimeList::checkIndex` logs a negative index as
  `sim.setup` at `error` and calls `eglog::flush()`, which flushes `std::cout` and `std::cerr`,
  before `std::abort()`: the sinks write `std::cout` without a flush. The command-line entry
  points (`app/main.cpp`, `scene/SceneTool.cpp`) write to `std::cerr`. `app/main.cpp` also
  writes the line `End of Simulation` of a run without a window to `std::cout` itself: scripts
  read it, so no logging rule may hide it. A run in the window logs the same line as `app.run`.
- The code in `simulation/` uses `sim.setup` for the set-up of a run (the selected case, the
  route set-up) and for problems found in the scene data or the model while it is built or
  while the blocking times are computed. It uses `sim.events` for virtual coupling and
  following mode, arrivals, departures and the failure to write an arrival message,
  `sim.step` at `debug`, which is off by default, for messages written once per step by a
  train in the trajectory code, and `sim.pax` for passengers. The application uses `app.run`
  for the banners and the progress of a run. `io/RttpXml.cpp` uses `io.railml`, and the legacy
  sender (`io/ExternalState.cpp` and the XML that `app/DispatchController.cpp` sends) uses
  `io.zmq`.
- `scene/`, `update/` and `telemetry/` neither log nor print, apart from their entry points.
  They return the problem, as a `SceneDiagnostic` or an `error` field (`UpdateCheckResult`).
- `tools/e2e/test_logging_contract.py` counts the console write statements per file
  (`cout`, `cerr`, `clog`, `printf` and `fprintf` to `stdout` or `stderr`)
  and compares them with the list `ALLOWED` in both directions. A new console write fails the
  test; when statements are removed, lower the entry of the file to the new number and delete it
  at zero. The writes that stay are counted in `ALLOWED` and named in a comment there: the
  command-line output of `app/main.cpp` and the marker lines of the profiling switches and of
  scripted runs (`QEGTRAIN_TIMING`, `QEGTRAIN_NATIVE_DETAIL`, `QEGTRAIN_PLAYBACK_PROFILE`,
  `QEGTRAIN_DIALOG_SUPPRESSED`), which start with a fixed word and carry no level or category.
  The files that write the console on purpose are listed in `EXEMPT_FILES` with the reason. The
  test also checks that the code still writes the stdout lines that scripts read.

New code follows the newer modules: return the problem to the caller, and print only at the
top level, with `qWarning()` in Qt code and `std::cerr` with `toDisplayText()` in
command-line code. A message from `simulation/` or `io/` that cannot be returned uses `EG_LOG`.
Do not add another logging helper.

## Tests

C++ tests live in `EGTRAIN/QEGTRAIN/tests/` as `test_<name>.cpp`, one executable each, with
shared data in `tests/fixtures/`. Register one in the `if(EGTRAIN_BUILD_TESTS)` block of the
root `CMakeLists.txt`. The executable links the library that holds the code it tests
([Libraries](../architecture/source-layout.md#libraries)), or lists the sources itself when
they are in no library, instead of linking the application. The telemetry tests that use a test
hook link `egtrain_telemetry_hooks`. The CTest name equals the target name:

```cmake
add_executable(test_timeformat ${SRC_DIR}/tests/test_timeformat.cpp)
target_link_libraries(test_timeformat PRIVATE egtrain_util)
add_test(NAME test_timeformat COMMAND test_timeformat)
```

Then add the name to `egtrain_label_tests(unit ...)` or `egtrain_label_tests(integration ...)`
lower in that block, and to the `gui` and `slow` lists where they apply
([Test labels](build-and-test.md#test-labels)). Configuration fails when a test has neither
`unit` nor `integration`, or both. A test that needs a Qt platform plugin also sets
`ENVIRONMENT "QT_QPA_PLATFORM=offscreen"`.

- Tests are plain programs with a `main()` and no test framework. Most define a local
  `expect(bool, const char*)` that prints `failed: <message>`, combine results with
  `ok &= expect(...)` and return a non-zero exit code on failure (`tests/test_timeformat.cpp`).
- Assertions must still run in a Release build, the default. The telemetry tests put
  `#undef NDEBUG` before `#include <cassert>` (`tests/test_telemetryqueue.cpp`).
- A logic change comes with the smallest test that fails without it. For a validation rule,
  build a model that breaks the rule and assert the diagnostic code, plus severity or path
  where they matter (`hasCodeAndSeverity` in `tests/test_scenevalidator.cpp`).
- The gate to run depends on the kind of change (UI or rendering, simulation or scene
  model, scene format); see [Verification Gates](build-and-test.md#verification-gates).

`tests/e2e/CMakeLists.txt` adds the sources of the scripted GUI scenarios to `QEGTRAIN` when
tests are built. `MainWindowTestAccess` is the friend of `MainWindow` for them, a scenario with its
own file there (`LegacyImport.cpp`, `SceneDrop.cpp`, `OutputFolder.cpp`, `SceneRender.cpp`,
`StationOverlay.cpp`, `TrackPreview.cpp`, `VisualPolish.cpp`, `EditorSmoke.cpp`,
`CreatorAcceptance.cpp`) is a member of
it, and the table in `tests/e2e/E2ERunner.cpp` starts a scenario from its `QEGTRAIN_E2E_` variable.
`PostRun.cpp` holds the autostart and the checks that follow a run, which the window calls through
the hooks of `MainWindowTestHooks`, and `Dialogs.cpp` answers the dialogs that the window opens. A source
that a build without tests compiles must not name a `QEGTRAIN_E2E_` variable, print an `E2E_` marker or
assert with `failures <<`: put such code in `tests/e2e` and give the window a hook.
`tools/e2e/test_no_e2e_in_product.py` fails otherwise.

## Avoid in new code

Application code outside `simulation/` and `io/third_party/` has no `std::list`, no
`::iterator` declarations and no `empty()` compared to 0 or 1. New code avoids these idioms:

- `std::list` for random-access data: use `std::vector`.
- An explicit `::iterator` loop where a range-for works: use a range-for or an `<algorithm>`
  function such as `std::count_if`.
- A bool compared to 0 or 1 (`Name.empty() != 1`): write `!Name.empty()`.
- `strcpy_s`, `sprintf_s` and `char` buffers: use `std::string` or `QString`.
- C-style casts: use `static_cast<T>`, or `qobject_cast` and `qgraphicsitem_cast` for Qt types.
- `0` or `NULL` as a null pointer (`QWidget* parent = 0`): use `nullptr`. `modernize-use-nullptr` checks it.
- New owning raw pointers and `new T[n]`: see [Ownership](#ownership).
- New global variables: pass state as arguments and return results, as
  `validateScene(const SceneModel&)` in `scene/SceneValidator.h` does.
- New `#define` constants: use `constexpr` with a `k` prefix.
- In `scene/`, a file name read as a path with `std::filesystem::path(text)` and a path turned into text with
  `path.string()`: MSVC uses the system code page for both. File names cross the scene API as UTF-8, as
  `QString::toStdString` gives them. Use `pathFromUtf8` from `scene/Utf8Path.h` and `path.u8string()`.
