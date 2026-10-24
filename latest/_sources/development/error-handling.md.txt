# Error handling

How EGTRAIN handles failure: which exceptions are caught and which escape, the fatal paths, what the user sees for each recoverable failure, the safety nets, and the crashes seen and reproduced on 2026-10-19. Paths are relative to `EGTRAIN/QEGTRAIN/`.

## In short

- Runs, run preparation, scene reading in the window, window events and both `main` functions catch exceptions; the tables below give where and what the user sees. On macOS the guard on window events (`GuardedApplication::notify`) sees nothing, so an exception that leaves a slot there still ends in `std::terminate`.
- The output folder of a run, the scene folder that `loadScene` reads and the folder of a legacy import are checked with the `std::error_code` overloads of `std::filesystem`, so a folder that cannot be created or read gives a diagnostic instead of an exception.
- The window build installs a crash handler (`app/CrashNote.cpp`, see [Crash notes](#crash-notes)): `std::set_terminate`, and signal handlers for SIGSEGV, SIGBUS, SIGILL, SIGFPE and SIGABRT. Headless runs and `scene_tool` have none. The other handler is `qInstallMessageHandler(qtMessageToLog)` (`widgets/ConsoleWidget.cpp`), which routes Qt messages to the log. An exception that passes the guards ends in `std::terminate` and `abort()`; the window build then leaves a crash note that the next start reports.

## Exceptions

### Where they are caught

| Area | Where | Behaviour |
| --- | --- | --- |
| Simulation run | `DispatchController::runSimulation` (`app/DispatchController.cpp`), around the stages in `runSimulationStages` | The run ends as failed. See [What happens when one is caught](#what-happens-when-one-is-caught). |
| Run preparation and scene reading in the window | `RunController::runScene` around `prepareScene`, `SceneSession::openSceneDirectory` around `readScene` | Becomes `run.prepare.exception` ("Cannot prepare the run: <what>") or `scene.read.exception` ("Cannot read the case: <what>"). |
| Window events | `GuardedApplication::notify` | Logged as `Unexpected error in <class> (event <type>): <what>` in `app.ui`, and the event counts as not handled. Not on macOS. |
| Process entry | `main` in `app/main.cpp` and in `scene/SceneTool.cpp` | Exit code 70 with an `app.ui` error line, or exit code 1 with `error: <what>`. |
| Telemetry | `telemetry/TelemetrySender.cpp` (`catch (...)` around every operation and the timer pump), `app/UpdateTelemetryController.cpp`, `app/RunController.cpp` | Fails closed and silently, by design. |
| Recovery copies | `recovery/RecoveryController.cpp` (thread start and worker loop), `recovery/RecoveryStore.cpp` | Turned into a "Cannot write the recovery copy" outcome. |
| Live stream | `io/LiveServer.cpp` (per operation and the thread body), `io/LiveCommand.cpp` (parses with exceptions off), `io/ExternalState.cpp` | Logged; the stream stops. |
| Scene JSON | `json::parse` in `scene/SceneModel.cpp`, `SceneBundle.cpp`, `SceneWriter.cpp`, `SceneCompatibility.cpp`; typed reads through `SceneFieldReader` | Becomes a `SceneDiagnostic`. |
| Number parsing | every `std::stoi`/`stod`/`stoll` is caught or its input is checked first | Becomes a diagnostic. |
| `map::at` | every `std::map::at` and `unordered_map::at` follows a lookup or a validation | No throw expected. |

### Filesystem failures that give diagnostics

| Check | Diagnostic | What the user sees |
| --- | --- | --- |
| The output folder of a run and its subfolders (`ensureDirectory` in `prepareScene`) | `run.output.folder`: "Cannot create the output folder <path>: <reason>" | Window: **Cannot Run Case** with that text, Run Results status "Failed", the case stays open. Without a window: the diagnostic on standard error and exit code 1. |
| `<output>/TEMP` of a run with `--detailed-trajectories` | none; the run throws "Cannot create the folder <path>: <reason>", which `runSimulation` catches | The run fails as below. |
| The scene folder in `loadScene` (`scene/SceneModel.cpp`) | `scene.dir.unreadable`: "Cannot read the case folder: <reason>"; a path that does not exist or is not a folder keeps `scene.dir.missing` | Window: **Cannot Open Case**. At the start, headless and in `scene_tool validate`, `pack` and `export`: the diagnostic and exit code 1. |
| The legacy folder of an import and its passenger files (`scene/SceneImporter.cpp`) | `scene.import.missing` with the reason; passenger files that cannot be checked count as absent | **Cannot Import Legacy Case**, or `scene_tool import` exits with 1. |

### Where they still escape

| Throw site | Reached from | Thread | Caught by |
| --- | --- | --- | --- |
| `scene/SceneExporter.cpp` `fs::exists`/`is_directory` without `error_code` | `scene_tool export` only | main | `scene_tool`'s `main`: exit code 1 |
| Range-for over `fs::directory_iterator(dir, ec)`: the increment still throws (`SceneImporter.cpp`, `SceneExporter.cpp`) | import and export; needs an I/O error during a listing | GUI or main | `GuardedApplication` (not on macOS) or `scene_tool`'s `main` |
| `std::bad_alloc` | Train vectors sized `ceil(duration / timestep)` (`simulation/builders/OperationsBuilder.cpp`, `simulation/RollingStock.cpp`), built on the GUI thread; the scene validator checks only that the duration is positive and finite | GUI, or main without a window | `RunController::runScene`: **Cannot Run Case**; without a window, `main`: exit code 70 |

### What happens when one is caught

| Where it is thrown | Outcome |
| --- | --- |
| A run, on the simulation `QThread` or with `-g 0` | `runSimulation` returns normally, so `SimulationWorker::simulationFinished` is still emitted. `lastRunError()` holds the text, `app.run` logs "The run stopped because of an error: <what>", and the live stream gets `run_cancelled` once the run has sent its hello frame, because contract version 1 has no event for a failed run. The window shows **Simulation Failed** ("The run stopped because of an error:" and the reason), the status bar says "Simulation failed", Run Results says "Failed" and the case stays open and editable. Without a window the exit code is 1. |
| A slot or event handler on the GUI thread | Linux and Windows: `GuardedApplication` logs it, the event counts as not handled, and an **Unexpected Error** box says that the case is still open and should be saved. Further exceptions while that box waits or is open are only logged. What the slot had done before the throw is not undone. macOS: `std::terminate` at the throw, because an exception cannot pass the frames of QtWidgets. |
| Startup, before `application.exec()` | `main` logs "EGTRAIN stopped because of an unexpected error: <what>" in `app.ui` and exits with 70. |
| `scene_tool` | `error: <what>` on standard error and exit code 1. |

An exception that reaches `std::terminate` leaves only the `libc++abi: terminating due to uncaught exception` line on standard error, which a user who starts the app from Finder never sees.

## Fatal paths

| Path | Where | Notes |
| --- | --- | --- |
| `std::abort()` | `BlockingTimeList::checkIndex` (`simulation/RollingStock.h`) on a negative index | The only deliberate abort. It logs and flushes first. |
| `_exit(127)` | `update/UpdateHelper.cpp`, in a forked child after `exec` fails | Correct use. |
| `assert` | `io/third_party/pugixml.cpp` | Compiled out in release builds. |
| `Q_ASSERT`, `qFatal`, `Q_UNREACHABLE`, `std::terminate` | none in the application code | |
| Uncaught exceptions | see [Where they still escape](#where-they-still-escape) | |
| Dangling scene items | `QGraphicsScene`'s BSP index | See below. |
| Fixed simulation arrays | `arcs_in_signalling_block_section[20]` is written without a bound check (`simulation/builders/InfrastructureBuilder.cpp`, `simulation/Signalling.cpp`) | Relies on the validator rejecting sections with more than 20 arcs. `IDConnectedBS` is checked, and delayed indexes are checked in `RollingStockOccupation.cpp` and `RollingStock.cpp`. |

### Scene items and the paint crash

The network view paints through Qt's BSP index. The index keeps a pointer to an item in every leaf that the item's rectangle covered when it was indexed. A paint dereferences each pointer in the leaves it visits (`QGraphicsSceneFindItemBspTreeVisitor::visit`, then `QGraphicsItem::topLevelItem`). A pointer is left behind when an item's rectangle shrinks or moves without `prepareGeometryChange()`, the item is then removed using its new rectangle, and it is deleted before the index runs again. Heap corruption that overwrites a leaf list has the same stack.

On `main`, every geometry setter of the custom items calls `prepareGeometryChange()` first. One hazard is structural: `TrainItemGroup::boundingRect()` returns `childrenBoundingRect()` (`graphics/items/TrainItemGroup.cpp`), so the group's rectangle changes whenever a body changes, and only callers that call `prepareForChildGeometryChange()` first (`graphics/NetworkPresenter.cpp`, `TrainItemGroup::applyViewScale`) keep the index right. Deleting a body or `removeFromGroup` does not notify the group.

The paint crash came from the selection effect. Qt rounds the bounds of an item with an enabled `QGraphicsEffect` to whole device pixels of each view (`QGraphicsItemPrivate::effectiveBoundingRect`), so the rectangle of a selected item in the index changed with zoom and scroll without `prepareGeometryChange()`, and a selected item removed and deleted after the view moved stayed in the leaves of its old rectangle (AddressSanitizer: read in `effectiveBoundingRect` under `QGraphicsView::paintEvent`, free in `~PassengerItem`). `HighlightEffect` is therefore disabled; it draws nothing itself, since `SelectionCueItem` shows the selection. `test_highlighteffect` checks that a selected item removed after a zoom change is no longer found in the scene.

The visual polish smoke also had a use after free: it added a temporary `SignalItem` to `NetworkPresenter::allSignals`, deleted it, pumped events during a live run and only then removed the pointer, so a frame drawn in between could call `setAspectCode` on freed memory (`NetworkPresenter::updateSignalling`). It was found by reading and did not reproduce. The test now removes the pointer before it deletes the item.

### Other hazards

- `Train::BrakingDistanceFastComputation` and `AccelerationTimeFollowingMode` (`simulation/RollingStockBraking.cpp`) divide by an arc count that is 0 when no arc ends between the two positions. The result is NaN, the comparisons that use it are false, and an approach time is silently not found. No crash and no log.
- `RunController::clearSimulationWorker` (`app/RunController.cpp`) waits for the worker thread without a timeout. A step that never checks the stop flag freezes the window when the run is stopped or the window closes.

## Recoverable failures

| Failure | User sees | Logged |
| --- | --- | --- |
| Scene cannot be read, in the window | "Cannot Open Case" box (`app/SceneSession.cpp`) with the first diagnostic, then the status bar "Could not open <file name>". The text says that the file is damaged or not a case, or, for a path that does not exist or cannot be read, that EGTRAIN could not read it (`app/CannotOpenMessage.h`). An exception while the scene is read gives the same box. | Yes |
| Scene of another version | Question box with Check for Updates or the upgrade offer | No |
| Scene named on the command line cannot be read | Box, then the default case opens (`main.cpp`) | Standard error |
| Default scene cannot be read at start | "Cannot Start EGTRAIN" box, exit 1 (`main.cpp`) | Standard error |
| Save fails | "Cannot Save Case" box with the first diagnostic. Three Save As errors call `QMessageBox::critical` directly (`SceneSession.cpp`), which bypasses dialog suppression and the log. | Yes, except those three |
| Legacy import fails | Status bar "Legacy import failed" and a box with the summary | Yes |
| Run refused by validation or preparation, also when the output folder cannot be created or the preparation throws | "Cannot Run Case" box with the first diagnostic; Run Results status "Failed" | Yes |
| Run stopped by an exception | "Simulation Failed" box; status bar "Simulation failed"; Run Results status "Failed" | Yes |
| Run with zero trains | Nothing visible: one error line in the Log dock (`DispatchController.cpp`) | Dock |
| Run ends without results and was not stopped | Run Results says "Failed" and the status bar says "Simulation ended without results" | No |
| Run output files cannot be written | Nothing: `ofstream::open` is not checked (for example `simulation/Simulation.cpp`), and the run reports success | One line for one file (`RollingStock.cpp`) |
| Result export fails | "Export failed" warning with the path, without the reason from the OS | No |
| Live stream cannot start | "Cannot Start EGTRAIN" box, exit 2 (`main.cpp`) | Standard error |
| Manual update check fails | Warning box with the network error | No |
| Automatic update check fails | Nothing visible (`qWarning`, `app/UpdateTelemetryController.cpp`) | Dock, category `app.ui` |
| Telemetry send or consent fails | Nothing, by design; a consent save error shows in the dialog | No |
| Autosave fails | Status bar "Autosave unavailable: ..." once per scene, overwritten by the next status message; during a pick of an incident target it waits until the pick ends (`app/MainWindow.cpp`) | No |
| A recovery copy cannot be removed | Status bar for 8 s (`MainWindow.cpp`) | No |

`UserPrompts::showError` (`widgets/UserPrompts.h`) logs `<title>: <message>` at error level in `app.ui`, then shows the box. In a run with dialogs suppressed it prints `QEGTRAIN_DIALOG_SUPPRESSED: <title>: <message>` to standard error instead of the box.

## Logging

- `util/Log.h` has the categories `sim.setup`, `sim.step`, `sim.events`, `sim.pax`, `io.railml`, `io.zmq`, `app.run`, `app.ui` and `update`, and the levels debug to error. `EGTRAIN_LOG` sets the rules.
- In the application the sink (`consoleLogSink`, `widgets/ConsoleWidget.cpp`) writes to the Log dock, to standard output or standard error, and to the file named by `EGTRAIN_LOG_FILE`. Without that variable nothing is written to disk.
- The dock is hidden at start (`app/MainWindow.cpp`, View menu or Ctrl+Shift+L), keeps its text in memory only and does not draw attention to an error.

## Safety nets

| Net | What it does | Limits |
| --- | --- | --- |
| Autosave and recovery copies (#858, `recovery/`) | Every 30 s with unsaved edits, writes a copy under `AppLocalDataLocation/Recovery/<instance>` (or `QEGTRAIN_RECOVERY_DIR`), held by a lock file. At the next start `RecoveryStore::scan()` offers the copies whose lock is free, and `RecoveryPrompt` says "EGTRAIN closed before this case was saved". A clean close removes the folder. | Off in offscreen and suppressed runs unless `QEGTRAIN_RECOVERY_DIR` is set. Nothing is written before the first autosave, and `scan()` deletes a folder without a copy, so a crash with a saved case or before the first autosave leaves no copy. The prompt says that the app crashed only when a crash note exists. |
| Crash note (`app/CrashNote.cpp`) | A fatal signal or an uncaught exception in the window build writes a note, and the next start reports it. See [Crash notes](#crash-notes). | No note for headless runs, `scene_tool`, a forced quit or a power loss. No stack on Windows. |
| Diagnostics consent (`telemetry/`) | With consent, sends enum-only events such as `operation.failed` with an operation (`scene_open`, `simulation`, `export`) and an error code (`invalid_input`, `unsupported_format`, `io_failure`, `internal_failure`). A run that an exception ends after it has started sends `simulation.failed` and `operation.failed` with `simulation` and `internal_failure`. | The consent text promises no native crash dumps and no raw error messages. Schema version 1 has no name for a crash, and new names need a new schema version (`docs/telemetry/README.md`). |
| Logging core (`util/Log.h`) | Categories, levels and one sink | No log file unless `EGTRAIN_LOG_FILE` is set. |
| Startup notice of the previous run | `reportInPlaceUpdate` (`update/UpdateNotices.cpp`) reads `update-record.txt` left by the previous process and shows a box at the next start. | Covers updates only. |

### Crash notes

`main` calls `installCrashHandler(crashNoteFolder())` once the `QApplication` exists. The folder is `QEGTRAIN_CRASH_DIR` when it is set; otherwise scripted runs and the offscreen and minimal platforms keep no notes, and a normal run uses `AppLocalDataLocation/Crashes`. At install time the handler formats the note path, `crash-<UTC start time>-<pid>.txt`, and a header with the version, Qt, the system and the executable.

- A fatal signal appends the header, `Signal: <name>` and, on macOS and Linux, `Stack:` with the frames of `backtrace_symbols_fd`. The handler does not allocate and runs on an alternate stack on the main thread. `backtrace` is called once at install so that the handler loads nothing, but it is not on the POSIX list of calls that are safe in a signal handler, so a corrupted heap can leave a partial note. It then raises the signal again with the default action, so the system still writes its own crash report and shows its own message.
- `std::terminate` appends the header and `Uncaught exception: <what()>`, then calls `abort()`, whose SIGABRT handler adds the stack.
- On Windows an unhandled exception filter writes `Exception: 0x<code>` and passes the exception on to the filter it replaced: the C++ runtime turns an uncaught C++ exception into `std::terminate`, and the system reports any other. The SIGABRT handler returns instead of raising again, so that `abort()` goes on to its fast-fail report. The runtime keeps the terminate handler per thread and only the main thread has it, so an exception on another thread leaves the exception code and SIGABRT but not its text. There is no stack.
- At the next start `takeCrashNote` renames each `crash-*.txt` to `reported-crash-*.txt` and keeps the newest 10 reported notes. The newest note is named in **Recover Unsaved Work** when copies can be recovered, otherwise in the notice "EGTRAIN Closed Unexpectedly" with **Show Crash Note**, and in a warning of the category `app.ui`.

No crash event is sent: schema version 1 of the diagnostics has no name for one, so the consent text ("no native crash dumps") stays true. `tests/test_crashnote.cpp` and `tools/e2e/recovery_smoke.py` crash child processes on purpose, so the system keeps crash reports of them.

## Crashes seen on 2026-10-19

Crash reports from `~/Library/Logs/DiagnosticReports` on a development Mac, and the reproductions made for #981 with the Qt 5.15.19 build of 946ee7eb. macOS keeps at most 25 reports per process name and may only update an existing report for a repeated crash, so the folder undercounts.

| Crash | Stack (own frames) | Trigger | Status on 946ee7eb |
| --- | --- | --- | --- |
| SIGABRT, uncaught `filesystem_error` "in create_directories: Not a directory" | `ensureDirectory` <- `DispatchController::prepareScene` <- `MainWindow::timedPreparation` <- `RunController::runScene` <- `QObject::event` (window); `prepareScene` <- `main` (headless) | Run when the output folder cannot be created | Reproduced every time, in the window and headless |
| SIGABRT on the `QThread`, uncaught `filesystem_error` "in create_directories: File exists" | `std::terminate` <- QtCore <- `_pthread_start` (throw site lost) | `--detailed-trajectories` run when `<output>/TEMP` cannot be created | Reproduced every time |
| SIGABRT, uncaught `filesystem_error` "in posix_stat: ... Permission denied" or "File name too long" | `std::__fs::filesystem::__status` <- `loadScene` <- `loadScenePath` <- `main` | Opening a scene whose path cannot be read, at start or with `scene_tool` | Reproduced every time; the window's File > Open shares the code but was not clicked |
| SIGABRT in `scene_tool` | `loadScene` <- `loadScenePath` <- `main`; legacy import on an unreadable folder | `scene_tool validate`, `pack`, `export`, `import` | Reproduced every time |
| SIGSEGV while painting the network view (12 reports, Qt 5, 20:31-20:45) | `QGraphicsItem::topLevelItem` <- BSP visitor <- `QGraphicsView::paintEvent` <- `NetworkView::viewportEvent` ... `MainWindowTestAccess::runVisualPolish` <- `MainWindow::waitForUpdates` | Visual polish smoke, live Copenhagen run at scale 2; binaries of an experiment on f299a252 with a dialog probe | Reproduced on that tree (3 of 4 runs, and 3 of 3 under AddressSanitizer, which reads a garbage item pointer in `QGraphicsItemPrivate::effectiveBoundingRect` after "Clear selection"). Not reproduced on 946ee7eb in 26 runs of the same phases. |
| SIGSEGV in `QGraphicsSceneBspTree::items` (Qt 6) | Same paint path, symbolised (`qgraphicsscene_bsp.cpp`) | Visual polish smoke, live phase at scale 1, Qt 6 build | Seen once; not reproduced |
| SIGBUS at window teardown (Qt 6) | `QObject::destroyed` of a `QExpandingLineEdit` <- `~QTableWidget` <- `~InfrastructurePanel` <- `~MainWindow` | Window closed while a table cell editor was open, in the creator acceptance smoke | Seen twice with Qt 6 only; cause inferred |
| SIGABRT in `qDetectCpuFeatures` (Qt 6) | Before `main` | Qt 6 test binaries started under a sandbox | Environment, not EGTRAIN code |

Five rounds of the CTest smokes, `headless_smoke.py` and `roundtrip_smoke.py`, ten runs of the live visual polish phases, and AddressSanitizer runs of the visual phases and of all committed scenes headless, produced no crash on 946ee7eb.
