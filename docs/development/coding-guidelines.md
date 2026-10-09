# Coding guidelines

This guide records the conventions that new code follows. The newer modules (`scene/`,
`update/`, `diagrams/`, `graphics/`, `widgets/`, `telemetry/`) set the style.
`simulation/` is legacy code with different naming, fixed-size global arrays and
`std::list` containers; new code follows the newer modules, including when it is added to
`simulation/`. These rules do not ask for a rename of legacy code.

Formatting follows `.clang-format` and `.editorconfig` in the repository root.
`clang-format --dry-run --Werror <files>` reports where a file differs from the style.
The vendored code in `io/third_party/` is listed in `.clang-format-ignore` and is not
reformatted.

`.clang-tidy` selects the static checks. CMake writes `build/compile_commands.json` with
the Makefile and Ninja generators, and `clang-tidy -p build <file>` reads it. With the
Homebrew LLVM on macOS, add `--extra-arg=-isysroot --extra-arg=$(xcrun --show-sdk-path)`
so that the standard library headers are found. `io/third_party/` has its own
`.clang-tidy` that switches the analysis off.

## Warnings

`egtrain_target_warnings(<target> [STRICT])` in the root `CMakeLists.txt` adds `-Wall -Wextra`
(`/W4` with MSVC) to the C++ sources of one target. The flags are private to the target, so a
target that links a library does not get them.

- Strict targets build without a warning: `egtrain_util`, `egtrain_scene`, `egtrain_update`,
  `egtrain_graphics`, `egtrain_widgets`, the `egtrain_telemetry` libraries, `egtrain_diagrams`,
  `scene_tool`, `egtrain_update_helper` and the tests that use only these libraries. When
  `EGTRAIN_WARNINGS_AS_ERRORS` is ON they also get `-Werror` (`/WX` with MSVC), so a new
  warning fails the build.
- `egtrain_dispatch` has the flags only. Its own sources are clean, and the warnings in its
  build log come from the simulation headers.
- These have no flags: the vendored `egtrain_pugixml` and `egtrain_miniz`, the legacy
  `egtrain_sim` and `egtrain_railml`, `QEGTRAIN`, and the test executables that use the
  simulation. A comment next to the calls gives the number of warnings.

To give a new target the flags, call `egtrain_target_warnings(<target> STRICT)` after its
`add_library` or `add_executable`, or add a test to the list of strict tests in the test block,
and fix its warnings first. A source that includes `simulation/RollingStock.h` or
`simulation/Signalling.h` in a strict target wraps the include in a diagnostic pragma, as
`diagrams/RunResults.cpp` does, because those headers still give warnings. A function that a
platform does not use, such as the update stagers in `update/UpdatePreparation.cpp`, is marked
`[[maybe_unused]]`.

To treat these warnings as errors in a local build, configure with the option:

```bash
cmake -S . -B build -DEGTRAIN_BUILD_TESTS=ON -DEGTRAIN_WARNINGS_AS_ERRORS=ON
```

The option is OFF by default, so a newer compiler cannot break a local build with a warning
that the code did not give before.

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
| `tests/` | C++ test executables, one per file, with data in `tests/fixtures/` |
| `tools/` | At the repository root: Python and shell tests and helpers (`e2e/`, `golden_master/`, `memory/`, `performance/`, `release/`) |

- Add a new `.cpp` to the library that holds its folder ([Libraries](../architecture/source-layout.md#libraries)),
  or to `EGTRAIN_SOURCES` when only the application uses it. Add a new `.h` to `EGTRAIN_HEADERS`
  unless it belongs to a library source. Both lists are in the root `CMakeLists.txt`.
- Include project headers with quotes and a path from `EGTRAIN/QEGTRAIN/`, for example
  `#include "scene/SceneModel.h"`. Qt and standard headers use angle brackets.
- `scene/` and `util/` contain no Qt types, and `scene_tool` links no Qt. Code that needs
  Qt goes elsewhere and converts at the boundary, for example with `QString::fromStdString`.
- New headers use `.h` and `#pragma once`. Guard styles are mixed today (`#ifndef` in
  `scene/`, `graphics/`, `diagrams/` and `update/`; `#pragma once` in `telemetry/` and part
  of `widgets/`), and `.hpp` appears only in older `util/` files and the vendored code.

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
- Class data members: `m_` prefix (`m_network`, `m_reply` in `update/UpdateChecker.h`). A
  few classes use a trailing underscore (`app/GuiReplayHistory.h`); new code uses `m_`.
  Public fields of plain data structs have no prefix (`SceneDiagnostic::itemId`).
- Constants are `constexpr` with a `k` prefix: `kUpdateCheckTimeoutMs` in an anonymous
  namespace in `update/UpdateChecker.cpp`, `kCurrentSceneSchemaVersion` as `inline constexpr`
  in `scene/SceneModel.h`. Helpers used by one `.cpp` file also go in an anonymous namespace.
- Mark overriding virtual functions `override`, as `graphics/items/StationOverlayItem.h`
  does; several older item classes in `graphics/items/` omit it.
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
  scene items held elsewhere only observe them: `MainWindow::teardownGUI` calls
  `scene->clear()`, which deletes the items, then clears the lists that point at them
  (`app/MainWindow.cpp`, documented in `app/MainWindow.h`).
- Everything else: `std::vector` for collections and arrays, `std::unique_ptr` for one owned
  object that is not a QObject. `SceneModel` holds its entity lists in `std::vector` members,
  `widgets/ConsoleWidget.h` owns a `std::unique_ptr<ConsoleStreambuf>`, and
  `telemetry/TelemetryQueue.h` owns a `std::unique_ptr<QLockFile>`.
- `simulation/` uses `new T[n]` with `delete[]`, fixed-size global arrays and `extern`
  globals. Do not add more. `regional_train` is a `std::vector<Regional>` created with the
  number of trains of the scene (`extern` in `simulation/RollingStock.h`); `Max_N_Reg` is
  only the limit that validation and the editor enforce on that number. A `Regional` owns
  its `Stations` buffer through a raw pointer, so the vector is created once per build and
  never grown, copied or reallocated while the trains are live.

`tools/memory/ownership_inventory.py` lists owning allocations, Qt parent allocations,
arrays of 100 or more elements and raw pointers, and its `CONFIRMED_OBSERVERS` table names
the pointers known to be non-owning. `test_ownership_inventory` runs the scanner on the real
tree and a fixture, and `test_raii_contract` checks that the delay statistics in
`simulation/Simulation.cpp` use `std::vector` instead of `new double[]` and that the retired
`setupEgtrain` and `prepareSimulation` stay out of `app/DispatchController.cpp`. When
`test_raii_contract` fails, remove the raw allocation or the restored path instead of
editing the test; when `test_ownership_inventory` fails after a pointer in
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

The code base has no single logging convention.

- `util/Logger.hpp` defines the `eglogger` macro and `app/main.cpp` defines a second
  `Logger`, `owl`; both appear in `simulation/` and `app/DispatchController.cpp`. Nothing
  calls `Logger::init`, so neither writes anything.
- `simulation/` prints to `std::cout` and `std::cerr`. `MainWindow` installs a Qt message
  handler that sends `qWarning()` and other Qt messages to `std::cout`, and `ConsoleWidget`
  shows both streams in the Console Log dock. The command-line entry points
  (`app/main.cpp`, `scene/SceneTool.cpp`) write to `std::cerr`.
- `scene/`, `update/` and `telemetry/` neither log nor print, apart from their entry points.
  They return the problem, as a `SceneDiagnostic` or an `error` field (`UpdateCheckResult`).

New code follows the newer modules: return the problem to the caller, and print only at the
top level, with `qWarning()` in Qt code and `std::cerr` with `toDisplayText()` in
command-line code. Do not add `eglogger` or `owl` calls or a new logging helper.

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

## Avoid in new code

Application code outside `simulation/` and `io/third_party/` has no `std::list`, no
`::iterator` declarations and no `empty()` compared to 0 or 1. New code avoids these idioms:

- `std::list` for random-access data: use `std::vector`.
- An explicit `::iterator` loop where a range-for works: use a range-for or an `<algorithm>`
  function such as `std::count_if`.
- A bool compared to 0 or 1 (`Name.empty() != 1`): write `!Name.empty()`.
- `strcpy_s`, `sprintf_s` and `char` buffers: use `std::string` or `QString`.
  `util/portability.h` maps `strcpy_s` to `strcpy` outside MSVC.
- C-style casts: use `static_cast<T>`, or `qobject_cast` and `qgraphicsitem_cast` for Qt types.
- `0` as a null pointer (`QWidget* parent = 0`): use `nullptr`.
- New owning raw pointers and `new T[n]`: see [Ownership](#ownership).
- New global variables: pass state as arguments and return results, as
  `validateScene(const SceneModel&)` in `scene/SceneValidator.h` does.
- New `#define` constants: use `constexpr` with a `k` prefix.
