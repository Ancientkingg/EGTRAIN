# Error handling

How EGTRAIN handles failure on `main` at 946ee7eb (2026-10-19): which exceptions are caught and which escape, the fatal paths, what the user sees for each recoverable failure, the safety nets, and the crashes seen and reproduced on that day. Paths are relative to `EGTRAIN/QEGTRAIN/`.

## In short

- Nothing catches an exception at the top. `main()` (`app/main.cpp`) has no `try`, `QApplication::notify` is not overridden, `SimulationWorker::run()` (`simulation/SimulationWorker.cpp:21-28`) has no `try`, and `scene_tool`'s `main()` (`scene/SceneTool.cpp:48`) has none either.
- The window build installs a crash handler (`app/CrashNote.cpp`, see [Crash notes](#crash-notes)): `std::set_terminate`, and signal handlers for SIGSEGV, SIGBUS, SIGILL, SIGFPE and SIGABRT. Headless runs and `scene_tool` have none. The other handler is `qInstallMessageHandler(qtMessageToLog)` (`widgets/ConsoleWidget.cpp:310`), which routes Qt messages to the log.
- So an exception that leaves a slot, an event handler, the startup code or the simulation thread ends in `std::terminate` and `abort()`. On macOS the user sees "QEGTRAIN quit unexpectedly". The app shows no message at the time and logs nothing, but the window build leaves a crash note that the next start reports. Unsaved edits survive only if an autosave copy exists.
- The exceptions that escape today all come from `std::filesystem` calls that take no `std::error_code`.

## Exceptions

### Where they are caught

| Area | Where | Behaviour |
| --- | --- | --- |
| Telemetry | `telemetry/TelemetrySender.cpp` (`catch (...)` around every operation and the timer pump), `app/UpdateTelemetryController.cpp:249`, `app/RunController.cpp:593-617` | Fails closed and silently, by design. |
| Recovery copies | `recovery/RecoveryController.cpp` (thread start and worker loop), `recovery/RecoveryStore.cpp` | Turned into a "Cannot write the recovery copy" outcome. |
| Live stream | `io/LiveServer.cpp` (per operation and the thread body), `io/LiveCommand.cpp` (parses with exceptions off), `io/ExternalState.cpp` | Logged; the stream stops. |
| Scene JSON | `json::parse` in `scene/SceneModel.cpp`, `SceneBundle.cpp`, `SceneWriter.cpp`, `SceneCompatibility.cpp`; typed reads through `SceneFieldReader` | Becomes a `SceneDiagnostic`. |
| Number parsing | every `std::stoi`/`stod`/`stoll` is caught or its input is checked first | Becomes a diagnostic. |
| `map::at` | every `std::map::at` and `unordered_map::at` follows a lookup or a validation | No throw expected. |

### Where they escape

| Throw site | Reached from | Thread | Reproduced |
| --- | --- | --- | --- |
| `app/DispatchController.cpp:37` `ensureDirectory`: `create_directories(path)` | `prepareScene` (`:326-330`), from Run (`RunController::runScene` through `MainWindow::timedPreparation`) and from headless `main` (`main.cpp:551`) | GUI or main | Yes |
| The same `ensureDirectory` for `<output>/TEMP` (`DispatchController.cpp:646`), only with `--detailed-trajectories` | `runSimulation` | Simulation `QThread` | Yes |
| `scene/SceneModel.cpp:775` `fs::is_directory`, `:839`, `:840`, `:844` `fs::exists` in `loadScene` | `loadScenePath` (`scene/SceneBundle.cpp:609`) from the startup preload (`main.cpp:457`), headless `main` (`:536`), `MainWindow::readScene` (`app/MainWindow.cpp:1739`, used by File > Open, recent cases, drops and relayed open requests), `scene_tool validate`, `pack` and `export` | GUI or main | Yes (startup and `scene_tool`) |
| `scene/SceneImporter.cpp:940` `fs::is_directory`, `:521`, `:526`, `:527` | Legacy import (`app/SceneSession.cpp:748`), Import Passengers (`editors/PassengerPanel.cpp:942`), `scene_tool import` | GUI | Yes (`scene_tool`) |
| `scene/SceneExporter.cpp` `fs::exists`/`is_directory` without `error_code` | `scene_tool export` only | main | Yes |
| Range-for over `fs::directory_iterator(dir, ec)`: the increment still throws (`SceneImporter.cpp`, `SceneExporter.cpp`) | import and export | GUI or main | No; needs an I/O error during a listing |
| `std::bad_alloc` | Train vectors sized `ceil(duration / timestep)` (`simulation/builders/OperationsBuilder.cpp:979`, `simulation/RollingStock.cpp`), built on the GUI thread; the scene validator checks only that the duration is positive and finite | GUI | No |

These calls throw `std::filesystem::filesystem_error` whenever `stat` or `mkdir` fails for any reason other than "not found": permission denied (also from macOS privacy protection), a name that is too long, a file where a folder should be, or an I/O error. `QDir().mkpath()` before the run (`app/RunController.cpp:771`, `app/main.cpp:321`) ignores its result, so the later `create_directories` is where it fails.

### What happens when one escapes

| Where it escapes | Outcome |
| --- | --- |
| A slot or event handler on the GUI thread | `std::terminate` at the throw, because the unwinder finds no handler. `QApplication::notify` is on the stack, so an override of it would catch these. |
| Startup, before `application.exec()` | `std::terminate` before the window shows. |
| The simulation `QThread` | Qt's thread start calls `std::terminate`. The crash report shows only `std::terminate` <- QtCore <- `_pthread_start`, so the throw site is lost; the crash note keeps the text of the exception. `SimulationWorker::simulationFinished` is never emitted. `QApplication::notify` is not on this stack (`QThread::started` calls `SimulationWorker::run` directly), so a `notify` override cannot catch it. |
| `scene_tool` | `std::terminate`; exit code 134. |

The only text is the `libc++abi: terminating due to uncaught exception` line on standard error, which a user who starts the app from Finder never sees.

## Fatal paths

| Path | Where | Notes |
| --- | --- | --- |
| `std::abort()` | `BlockingTimeList::checkIndex` (`simulation/RollingStock.h:132-137`) on a negative index | The only deliberate abort. It logs and flushes first. |
| `_exit(127)` | `update/UpdateHelper.cpp`, in a forked child after `exec` fails | Correct use. |
| `assert` | `io/third_party/pugixml.cpp` | Compiled out in release builds. |
| `Q_ASSERT`, `qFatal`, `Q_UNREACHABLE`, `std::terminate` | none in the application code | |
| Uncaught exceptions | see [Exceptions](#exceptions) | |
| Dangling scene items | `QGraphicsScene`'s BSP index | See below. |
| Fixed simulation arrays | `arcs_in_signalling_block_section[20]` is written without a bound check (`simulation/builders/InfrastructureBuilder.cpp:848-849`, `simulation/Signalling.cpp`) | Relies on the validator rejecting sections with more than 20 arcs. `IDConnectedBS` is checked, and delayed indexes are checked in `RollingStockOccupation.cpp` and `RollingStock.cpp`. |

### Scene items and the paint crash

The network view paints through Qt's BSP index. The index keeps a pointer to an item in every leaf that the item's rectangle covered when it was indexed. A paint dereferences each pointer in the leaves it visits (`QGraphicsSceneFindItemBspTreeVisitor::visit`, then `QGraphicsItem::topLevelItem`). A pointer is left behind when an item's rectangle shrinks or moves without `prepareGeometryChange()`, the item is then removed using its new rectangle, and it is deleted before the index runs again. Heap corruption that overwrites a leaf list has the same stack.

On `main`, every geometry setter of the custom items calls `prepareGeometryChange()` first. One hazard is structural: `TrainItemGroup::boundingRect()` returns `childrenBoundingRect()` (`graphics/items/TrainItemGroup.cpp:28-30`), so the group's rectangle changes whenever a body changes, and only callers that call `prepareForChildGeometryChange()` first (`graphics/NetworkPresenter.cpp`, `TrainItemGroup::applyViewScale`) keep the index right. Deleting a body or `removeFromGroup` does not notify the group.

The visual polish smoke had a use after free: it added a temporary `SignalItem` to `NetworkPresenter::allSignals`, deleted it, pumped events during a live run and only then removed the pointer, so a frame drawn in between could call `setAspectCode` on freed memory (`NetworkPresenter::updateSignalling`). It was found by reading and did not reproduce. The test now removes the pointer before it deletes the item.

### Other hazards

- `Train::BrakingDistanceFastComputation` and `AccelerationTimeFollowingMode` (`simulation/RollingStockBraking.cpp:15-63`) divide by an arc count that is 0 when no arc ends between the two positions. The result is NaN, the comparisons that use it are false, and an approach time is silently not found. No crash and no log.
- `RunController::clearSimulationWorker` (`app/RunController.cpp:137-143`) waits for the worker thread without a timeout. A step that never checks the stop flag freezes the window when the run is stopped or the window closes.

## Recoverable failures

| Failure | User sees | Logged |
| --- | --- | --- |
| Scene cannot be read, in the window | "Cannot Open Case Study" box (`app/SceneSession.cpp:340-351`) with the first diagnostic, then the status bar "Scene unchanged: ... could not be opened." The text always says the file is damaged or not a case study (`app/CannotOpenMessage.h`), also for an I/O error. | No |
| Scene of another version | Question box with Check for Updates or the upgrade offer | No |
| Scene named on the command line cannot be read | Box, then the default case opens (`main.cpp:484-491`) | Standard error |
| Default scene cannot be read at start | "Cannot Start EGTRAIN" box, exit 1 (`main.cpp:492-497`) | Standard error |
| Save fails | "Cannot Save Scene" box with the first diagnostic. Three Save As errors call `QMessageBox::critical` directly (`SceneSession.cpp:511, 520, 532`), which bypasses dialog suppression. | No |
| Legacy import fails | Status bar "Legacy import failed. Scene unchanged." and a box with the summary | No |
| Run refused by validation or preparation | "Cannot Run Scene" box with the first diagnostic; Run Results status "Failed" | No (telemetry only) |
| Run with zero trains | Nothing visible: one error line in the Console Log dock (`DispatchController.cpp:539`) | Dock |
| Run ends without results and was not stopped | Run Results says "Failed", but the status bar says "Simulation complete - open the Diagrams menu for results" (`RunController.cpp:681-686`) | No |
| Run output files cannot be written | Nothing: `ofstream::open` is not checked (for example `simulation/Simulation.cpp:199`), and the run reports success | One line for one file (`RollingStock.cpp:614`) |
| Result export fails | "Export failed" warning with the path, without the reason from the OS | No |
| Live stream cannot start | "Cannot Start EGTRAIN" box, exit 2 (`main.cpp:288-300`) | Standard error |
| Manual update check fails | Warning box with the network error | No |
| Automatic update check fails | Nothing visible (`qWarning`, `app/UpdateTelemetryController.cpp:330`) | Dock, category `app.ui` |
| Telemetry send or consent fails | Nothing, by design; a consent save error shows in the dialog | No |
| Autosave fails | Status bar "Autosave unavailable: ..." once per scene, overwritten by the next status message (`app/MainWindow.cpp:2075-2080`) | No |
| A recovery copy cannot be removed | Status bar for 8 s (`MainWindow.cpp:2137`) | No |

`UserPrompts::showError` (`widgets/UserPrompts.h:23-33`) shows the box and logs nothing. In a run with dialogs suppressed it prints `QEGTRAIN_DIALOG_SUPPRESSED: <title>: <message>` to standard error instead.

## Logging

- `util/Log.h` has the categories `sim.setup`, `sim.step`, `sim.events`, `sim.pax`, `io.railml`, `io.zmq`, `app.run`, `app.ui` and `update`, and the levels debug to error. `EGTRAIN_LOG` sets the rules.
- In the application the sink (`consoleLogSink`, `widgets/ConsoleWidget.cpp:258-278`) writes to the Console Log dock, to standard output or standard error, and to the file named by `EGTRAIN_LOG_FILE`. Without that variable nothing is written to disk.
- The dock is hidden at start (`app/MainWindow.cpp:1102`, View menu or Ctrl+Shift+L), keeps its text in memory only and does not draw attention to an error.

## Safety nets

| Net | What it does | Limits |
| --- | --- | --- |
| Autosave and recovery copies (#858, `recovery/`) | Every 30 s with unsaved edits, writes a copy under `AppLocalDataLocation/Recovery/<instance>` (or `QEGTRAIN_RECOVERY_DIR`), held by a lock file. At the next start `RecoveryStore::scan()` offers the copies whose lock is free, and `RecoveryPrompt` says "EGTRAIN closed before this case was saved". A clean close removes the folder. | Off in offscreen and suppressed runs unless `QEGTRAIN_RECOVERY_DIR` is set. Nothing is written before the first autosave, and `scan()` deletes a folder without a copy, so a crash with a saved case or before the first autosave leaves no copy. The prompt says that the app crashed only when a crash note exists. |
| Crash note (`app/CrashNote.cpp`) | A fatal signal or an uncaught exception in the window build writes a note, and the next start reports it. See [Crash notes](#crash-notes). | No note for headless runs, `scene_tool`, a forced quit or a power loss. No stack on Windows. |
| Diagnostics consent (`telemetry/`) | With consent, sends enum-only events such as `operation.failed` with an operation (`scene_open`, `simulation`, `export`) and an error code (`invalid_input`, `unsupported_format`, `io_failure`, `internal_failure`). | The consent text promises no native crash dumps and no raw error messages. Schema version 1 has no name for a crash, and new names need a new schema version (`docs/telemetry/README.md`). `SimulationObservation::Outcome::Failed` exists but is never used. |
| Logging core (`util/Log.h`) | Categories, levels and one sink | No log file unless `EGTRAIN_LOG_FILE` is set. |
| Startup notice of the previous run | `reportRolledBackUpdate` (`update/UpdateNotices.cpp:10-32`) reads `update-record.txt` left by the previous process and shows a box at the next start. | Covers updates only. |

### Crash notes

`main` calls `installCrashHandler(crashNoteFolder())` once the `QApplication` exists. The folder is `QEGTRAIN_CRASH_DIR` when it is set; otherwise scripted runs and the offscreen and minimal platforms keep no notes, and a normal run uses `AppLocalDataLocation/Crashes`. At install time the handler formats the note path, `crash-<UTC start time>-<pid>.txt`, and a header with the version, Qt, the system and the executable.

- A fatal signal appends the header, `Signal: <name> (<number>)` and, on macOS and Linux, `Stack:` with the frames of `backtrace_symbols_fd`. The handler does not allocate and runs on an alternate stack on the main thread. `backtrace` is called once at install so that the handler loads nothing, but it is not on the POSIX list of calls that are safe in a signal handler, so a corrupted heap can leave a partial note. It then raises the signal again with the default action, so the system still writes its own crash report and shows its own message.
- `std::terminate` appends the header and `Uncaught exception: <what()>`, then calls `abort()`, whose SIGABRT handler adds the stack.
- On Windows an unhandled exception filter writes `Exception: 0x<code>` and passes the exception on to the filter it replaced: the C++ runtime turns an uncaught C++ exception into `std::terminate`, and the system reports any other. There is no stack.
- At the next start `takeCrashNote` renames each `crash-*.txt` to `reported-crash-*.txt` and keeps the newest 10 reported notes. The newest note is named in **Recover Unsaved Work** when copies can be recovered, otherwise in the notice "EGTRAIN Closed Unexpectedly" with **Show Crash Note**, and in a warning of the category `app.ui`.

No crash event is sent: schema version 1 of the diagnostics has no name for one, so the consent text ("no native crash dumps") stays true. `tests/test_crashnote.cpp` and `tools/e2e/recovery_smoke.py` crash child processes on purpose, so the system keeps crash reports of them.

## Crashes seen on 2026-10-19

Crash reports from `~/Library/Logs/DiagnosticReports` on a development Mac, and the reproductions made for #981 with the Qt 5.15.19 build of 946ee7eb. macOS keeps at most 25 reports per process name and may only update an existing report for a repeated crash, so the folder undercounts.

| Crash | Stack (own frames) | Trigger | Status on `main` |
| --- | --- | --- | --- |
| SIGABRT, uncaught `filesystem_error` "in create_directories: Not a directory" | `ensureDirectory` <- `DispatchController::prepareScene` <- `MainWindow::timedPreparation` <- `RunController::runScene` <- `QObject::event` (window); `prepareScene` <- `main` (headless) | Run when the output folder cannot be created | Reproduced every time, in the window and headless |
| SIGABRT on the `QThread`, uncaught `filesystem_error` "in create_directories: File exists" | `std::terminate` <- QtCore <- `_pthread_start` (throw site lost) | `--detailed-trajectories` run when `<output>/TEMP` cannot be created | Reproduced every time |
| SIGABRT, uncaught `filesystem_error` "in posix_stat: ... Permission denied" or "File name too long" | `std::__fs::filesystem::__status` <- `loadScene` <- `loadScenePath` <- `main` | Opening a scene whose path cannot be read, at start or with `scene_tool` | Reproduced every time; the window's File > Open shares the code but was not clicked |
| SIGABRT in `scene_tool` | `loadScene` <- `loadScenePath` <- `main`; legacy import on an unreadable folder | `scene_tool validate`, `pack`, `export`, `import` | Reproduced every time |
| SIGSEGV while painting the network view (12 reports, Qt 5, 20:31-20:45) | `QGraphicsItem::topLevelItem` <- BSP visitor <- `QGraphicsView::paintEvent` <- `NetworkView::viewportEvent` ... `MainWindowTestAccess::runVisualPolish` <- `MainWindow::waitForUpdates` | Visual polish smoke, live Copenhagen run at scale 2; binaries of an experiment on f299a252 with a dialog probe | Reproduced on that tree (3 of 4 runs, and 3 of 3 under AddressSanitizer, which reads a garbage item pointer in `QGraphicsItemPrivate::effectiveBoundingRect` after "Clear selection"). Not reproduced on 946ee7eb in 26 runs of the same phases. |
| SIGSEGV in `QGraphicsSceneBspTree::items` (Qt 6) | Same paint path, symbolised (`qgraphicsscene_bsp.cpp:74`) | Visual polish smoke, live phase at scale 1, Qt 6 build | Seen once; not reproduced |
| SIGBUS at window teardown (Qt 6) | `QObject::destroyed` of a `QExpandingLineEdit` <- `~QTableWidget` <- `~InfrastructurePanel` <- `~MainWindow` | Window closed while a table cell editor was open, in the creator acceptance smoke | Seen twice with Qt 6 only; cause inferred |
| SIGABRT in `qDetectCpuFeatures` (Qt 6) | Before `main` | Qt 6 test binaries started under a sandbox | Environment, not EGTRAIN code |

Five rounds of the CTest smokes, `headless_smoke.py` and `roundtrip_smoke.py`, ten runs of the live visual polish phases, and AddressSanitizer runs of the visual phases and of all committed scenes headless, produced no crash on 946ee7eb.
