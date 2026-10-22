# Smoke tests

This page describes the smoke tests that run the application end to end on the committed scenes, and the
leak and memory checks. It ends with where the tests and runs write their output.

## Simulation Smoke Test

```bash
tools/e2e/headless_smoke.py
```

The smoke test runs Netherlands (`-n 1`), Paimpol (`-n 2`), Copenhagen
(`-n 3`), Brescia (`-n 4`), Assignment (`-n 5`), and
Amsterdam_Hilversum_Student, which has no `-n` number and is case 7 of the
script. It checks clean native execution and the available trajectory/station
evidence. For every scene it also requires that no train moves farther in one
step than the highest maximum speed of the scene's rolling stock allows, and
that no train steps against the direction of its route by more than 0.1 m. The
direction comes from the flag in the train's row in
`TrainTrajectories/TrainServicePathDiagram.txt`. The script starts every run with
`--detailed-trajectories`, because those checks read `TEMP/Traj_Train_*.txt`.
Every scene run must also write section records to `BlockingTimes.txt`.
CTest runs the position checks in `test_headless_smoke_decode`
on trajectory and timetable files that the test writes.

The script runs the application that `QEGTRAIN_APP` names. Without that variable it uses
the build folder of the system: `build/QEGTRAIN.app/Contents/MacOS/QEGTRAIN` on macOS,
`build/QEGTRAIN` on Linux, and `build/QEGTRAIN.exe` or `build/Release/QEGTRAIN.exe` on Windows.
If none exists it stops and names the paths it tried.
CTest runs each case as its own test, `test_headless_smoke_case_1` to `test_headless_smoke_case_7`,
so that the cases run side by side.

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
of the CMake workflow, `leak check (Linux)`. It builds the application,
`test_characterization`, `test_networkpresenter`, `test_runtimecanvas` and `test_guisimulationsnapshot` in a
Debug configuration with `-DEGTRAIN_ENABLE_SANITIZERS=ON`, starts them with `ASAN_OPTIONS=detect_leaks=1`,
and fails on any leak or double free. It has four parts:

- `tools/e2e/leak_check_smoke.py` runs every scene directory below `EGTRAIN/QEGTRAIN/Scenes`
  headless, once, with `-h 600 --detailed-trajectories`. A scene fails when its log holds a
  LeakSanitizer or AddressSanitizer report, when the process exits with a code other than 0, or
  when it prints no `End of Simulation`. All scenes run before the script fails, so one start lists
  every failing scene.
- CTest runs `characterization_repeatable`, `characterization_repeat_paimpol`,
  `characterization_repeat_milano_paimpol` and `characterization_repeat_netherlands`. Each builds
  and runs scenes one after the other in one process (see [Characterization tests](characterization-tests.md#characterization-tests)),
  which is the scene reload. They cover the line fixture, Paimpol, Assignment,
  Milano-Brescia and Netherlands. Copenhagen and Amsterdam_Hilversum_Student run headless once and are
  not reloaded.
- CTest runs `test_networkpresenter` and `test_runtimecanvas` on the offscreen platform. They draw the
  track preview and the items of a run on a canvas, apply the steps of a run and of its replay with a
  selection on it, and clear the canvas again, twenty times or more in one process. After each clear
  every registry, every pointer and the scene must be empty, and an item or container that the clear
  does not release is a leak.
- CTest runs `test_guisimulationsnapshot`. It records, evicts and clears the frames of the replay
  history and compares the bytes the history counts with the bytes it holds on the heap. Under the
  sanitizer the heap count comes from AddressSanitizer.

The job runs only in the nightly and the manual run, because the sanitizer build and the
instrumented runs take longer than the pull request checks. To check a branch before a merge,
start the workflow by hand from the Actions tab, or with `gh workflow run cmake.yml --ref BRANCH`;
the other legs start with it. The artifact `leak-check-logs` holds one `SCENE.log` per scene and
`ctest-reload.log`, `ctest-canvas.log` and `ctest-replay.log`, also when the job fails.

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
entity counts for all six canonical scenes, then runs the small Assignment
reimport. It checks the compatibility rows in `TrackLines/AreasCaseStudy.txt`
and requires the ordered signalling areas, with their exported track scopes,
to survive through `TrackLines/EGTRAINSignallingAreas.json`. The import report
must account for every area. The importer warns about historical area files
without this sidecar and does not convert their rows. For each scene with
`views.json` it also compares the station and track views, the base time and
the simulation settings of the reimported scene with the source. Normal runs
load the canonical source directory directly.
The reimported Assignment run must report no route section without a
signalling level and must write section records to `BlockingTimes.txt`.
The script finds the application and `scene_tool` as the headless smoke finds the application, with
`QEGTRAIN_APP` and `QEGTRAIN_SCENE_TOOL` as the variables. CTest runs it as `test_roundtrip_smoke`.

CTest also runs three smaller scripts of the Assignment scene: `test_bundle_smoke` packs and validates
every committed scene with `bundle_smoke.py` and runs the Assignment bundle, `test_incident_smoke` runs
Assignment with a train breakdown and with a signal failure through `incident_smoke.py`, and
`test_assignment_smoke` checks the service ids, entry times and headway of its timetable with
`assignment_smoke.py`.

## Live stream tests

Five tests cover the live stream of `--live`. The promises they check are in [Live contract version 1](../architecture/external-sharing.md#live-contract-version-1).

```bash
ctest --test-dir build -R '^test_live' --output-on-failure
```

| Test | Label | What it covers |
| --- | --- | --- |
| `test_liveframe` | unit | The frames of `io/LiveFrame.h`: frames from hand-built snapshots, the rules for `null`, every frame against `docs/live/frame-v1.schema.json`, and the frames of a run of the line fixture against the golden files in `tests/live/expected`. A frame with a member that the schema does not name fails. |
| `test_livecommand` | unit | `parseLiveRequest` in `io/LiveCommand.h`: every accepted command, every reason, the limits of size, id and delay, the order of the checks and the id that comes back. It also checks the event frame that reports a command. It uses no socket, thread or run. |
| `test_liveserver` | unit | `LiveServer` with a synthetic producer and ZeroMQ clients on the loopback address: the endpoint file, the order of frames, the drop policy, a subscriber that never reads, disconnect and reconnect, restart, a port in use, bad requests, a full command queue, the final flush, shutdown with clients connected and producers on four threads. |
| `test_livereplay` | integration | The commands of a client against a run of the line fixture in the test process. See below. |
| `test_live_stream_smoke` | integration | The application with `--live` and `build/live_client`: a client that follows a run, sends `set_delay_ms` or leaves, a run that nobody reads, repeated services, a late client, a stopped run, a second run (macOS only) and the options that are refused. Each run with a stream is compared file by file, and by standard output, with a run without one. |

`test_livereplay` starts a `LiveServer` with commands inside the test process and acts as the client. It plays the JSON-lines files in `tests/live/commands` against the line fixture. Each line is `{"step": N, "cmd": ..., "args": ...}`. The test sends it from the thread of the run through a REQ socket after step N-1, so the run takes the command at the top of step N. A reader on another thread follows the stream, and it ends each pause with a `resume` once the `applied` event for the pause has come and the run has stood still for a while. The test checks:

- A run that receives `pause`, `resume` and `set_delay_ms` equals a run that receives nothing: every file of the output folder, the standard output and the state at each of the 1500 steps. A second run without commands is compared first, to show that a difference comes from the commands.
- The `applied` and `rejected` events with their step numbers, and that no `state` frame comes while the run is paused.
- A `stop` before step 100 ends the run as `cancelled` after 100 steps.
- A command that is accepted before the run is turned down with `no_active_run` and does not reach the run, and one that is accepted after the last step is turned down with `run_ended`.
- A command is turned down with `queue_full` when the control of the run holds 16 commands already.

To add a case, add a line to a file in `tests/live/commands` and the events it should give to `testReplay` in `tests/test_livereplay.cpp`.

### ThreadSanitizer

CI runs no ThreadSanitizer. Run the live code under it before a change to the threads of the stream (the server, the step loop in `DispatchController` or `RunControl`), in a build directory of its own:

```bash
cmake -S . -B build-tsan -DEGTRAIN_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS=-fsanitize=thread -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=thread
cmake --build build-tsan --target QEGTRAIN live_client test_liveserver test_livecommand test_livereplay
ctest --test-dir build-tsan -R '^test_live(server|command|replay)$' --output-on-failure
```

Qt is not instrumented, so a window run reports data races in `ConsoleWidget` and in Qt's queued calls. They do not involve the live code. A run under ThreadSanitizer takes far longer than the limits of `test_live_stream_smoke`. The smoke test stretches its waits by `EGTRAIN_TEST_TIMEOUT_SCALE`, so configure with `-DEGTRAIN_TEST_TIMEOUT_SCALE=100` before you run it, and put `log_path` in `TSAN_OPTIONS` to keep the reports.

## Completed-run replay

What the Replay row shows and how a user works with it is in
[Replay a completed run](../guides/how-to.md#replay-a-completed-run). This section
keeps what a developer needs.

The label beside the slider names the selected frame time and keeps the width of
its longest text, so the slider does not change its width while the selection
moves. The text below the controls does not depend on the selected time, so the
row keeps its height while the user seeks. A replay seek explicitly restores
historical operational overlays; normal completion does not. Future or exited
trains do not appear on layer toggles or drive Follow station emphasis.

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
committed scenes have one.

| Scene | Frames | Covers (s) | Accounted | As complete snapshots |
|---|---|---|---|---|
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

What a user sees when a command waits for a run to stop is in
[Close, start a new case or open another during a run](../guides/how-to.md#close-start-a-new-case-or-open-another-during-a-run).
`test_close_during_run_smoke` closes the window during an autostarted
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
validates, saves and reloads cases through the editors. When the first scene has
no passengers, it turns the passengers on in `Paimpol` and checks that the first
scene then draws no platform bars and shows the switch off, and that `Paimpol`
shows it on again. It then edits a copy of
the first scene in each editor and checks that every edit is one step of the
history, that Undo and Redo return the scene to the state before and after each
step (the bytes that Save writes, the editors, the validation, the unsaved mark
and the run results) and keep the zoom and the centre of the network view, that an
edit in a replay closes the Train Info dock of a train that is selected in it, that an edit
after a run replaces the message that the run is complete and gives the focus that was in the
Run Results dock to the network view, that a stop dialog that is accepted is one step and one
that is cancelled is none, that Save keeps the history, and that a new edit after
Undo, a failed open, an open and a new scene change the history as
[Window flow](../architecture/scene-editor-flow.md#window-flow) describes. It
then names a folder for recovery copies of its own and checks autosave on a
bundle copy of the scene (`E2E_EDITOR_RECOVERY_OK`): that a run with no folder
named keeps no copies; that an edit leads to one copy that names the scene and its
source and holds the edits, while the file of the scene stays as it was; that Undo
to the saved state, Save, an open of the same scene and a new scene each remove or
replace the copy; that Discard followed by an open that fails and Cancel keep it; that
closing the window leaves no copy; that Decide Later keeps a copy that an ended
window left and Discard removes it; that Recover opens the copy as an unsaved scene
without a file, with Save As starting at the original; that a folder that cannot
be made is reported once while the editing goes on; and that a copy that fails during
a pick of an incident target is reported when the pick ends, not over its hint. Next
it checks the layout of the editors: the main window does not grow beyond 1280
by 800 pixels when an editor dock is shown at the normal and at 1.5 times the
font size, a floated dock stays within 750 by 600 and keeps its scroll area,
the Add button of the Passengers editor is in view, and the buttons of the Infrastructure editor for Blocks and
Routes sit under its table without sideways scrolling. In the docked editor all of them, including the four buttons of the
route sections, are in the first view. In the floating dock of 750 by 600 the table buttons and the block buttons are in
the first view, and the route section buttons are reached by scrolling to the end of the editor. Last, it opens each editor dock, moves the focus
through its fields with Tab, scrolls, floats and resizes the dock, resizes the
main window and closes and reopens the dock, and requires that the modified
flag and the revision of the scene stay as they were.

The smoke passes when the application exits with status 0, prints
`E2E_EDITOR_SMOKE_OK` and prints the marker of every facet, the last being
`E2E_EDITOR_UNTOUCHED_OK`. The application output goes to
`${TMPDIR:-/tmp}/qegtrain-editor-smoke-e2e.log`. On a failure the script prints
the end of that log, which holds `E2E_EDITOR_SMOKE_FAIL: <facet>: <message>`
when the application reported the failure.

CTest runs the script as `test_editor_smoke` on macOS and Linux with
`QT_QPA_PLATFORM=offscreen`. Run it alone with
`ctest --test-dir build -R test_editor_smoke --output-on-failure`. The script
takes the application from `QEGTRAIN_APP`. The release workflow also runs it as
a step of its own.

## Recovery Smoke Test

```bash
python3 tools/e2e/recovery_smoke.py build/QEGTRAIN.app/Contents/MacOS/QEGTRAIN build/scene_tool
```

The script ends a window with unsaved edits without closing it and starts the
application again. Every run of the application gets a recovery folder and a
settings folder of its own (`QEGTRAIN_RECOVERY_DIR` and `QEGTRAIN_E2E_SETTINGS_DIR`),
so no run meets the copies of a user, and an interval of 100 ms
(`QEGTRAIN_RECOVERY_INTERVAL_MS`). `QEGTRAIN_E2E_RECOVERY` names the script that the
application runs in the window, and `QEGTRAIN_E2E_RECOVERY_ANSWER` and
`QEGTRAIN_E2E_RECOVERY_SAVE_AS` say how the next run answers the recovery prompt and
where it saves the recovered case. A run that ends in a crash makes three edits to a
bundle copy of `Assignment_Gvc_Gdg_Ut`, waits until the autosave has written its
copy and ends with `_Exit(137)`, so no destructor runs and no close event reaches the
window. The script checks:

- Bundle: the next start offers the copy with the path of the bundle and no note,
  with Recover as the default button. Recover opens an unsaved scene without a
  file that holds the three edits, Save As starts in the folder of the bundle with its
  file name, and the bundle keeps its bytes and its modification time. The window
  closes after Save As, no recovery folder is left and the next start has no prompt.
- New case: a case that was never saved is offered as `New case study, not saved` and
  recovered in the same way.
- Changed original: an original that changed, is newer than the copy or is missing is
  named in the prompt, and Decide Later is the default button for the first two.
  Decide Later keeps the copy, Recover opens it, Discard removes it, and the original is
  not touched.
- Discard at close: Discard in the question about unsaved changes leaves no copy, and
  the next start has no prompt.
- Unwritable folder: when the folder for the copies cannot be made, the window reports
  it once, the edits go on and a run completes.

CTest runs the script as `test_recovery_smoke` on macOS and Linux, with the
application and `scene_tool` from the build, and it takes about 25 seconds. A
failure prints the end of the output of the run that failed. The scripts of the
application are in `EGTRAIN/QEGTRAIN/tests/e2e/Recovery.cpp` and the answer to the
prompt in `Dialogs.cpp`. The editor smoke covers the same paths in one process.

The recovery folder is below the local application data folder of the user. An
operating system backup or a sync tool that includes that folder can copy recovery
files, which hold the case data of the scenes in them.

## GUI Smoke Test

```bash
tools/e2e/visual_polish_smoke.sh
```

Run this after UI or rendering changes. The script runs in phases, one after another, and
runs only the phases named on its command line, for example
`tools/e2e/visual_polish_smoke.sh signals stations`. Each phase writes file names of its own below
`${TMPDIR:-/tmp}`, so phases do not disturb each other. On macOS CTest runs the phases as five
tests that can run side by side, and `ctest --test-dir build -R test_visual_polish_smoke
--output-on-failure` starts all five:

| Test | Phase | What it runs |
| --- | --- | --- |
| `test_visual_polish_smoke_live` | `dpr1` | The live run of Copenhagen at a device pixel ratio of 1: screenshots, selection cues, the Follow checks, the labels of the timetable window and the absence of the Show Graph action |
| | `color` | A copy of Copenhagen in which the first service has a colour |
| | `dpr2` | The live run of Copenhagen at a device pixel ratio of 2 |
| `test_visual_polish_smoke_replay` | `completion` | The Assignment run to its end: the replay row and the Follow checks of the replay |
| | `replay` | The whole-run replay of Assignment, Paimpol, Milano_Brescia and Netherlands |
| | `discard` | The preview legend after a discarded run |
| `test_visual_polish_smoke_replay_copenhagen` | `replay-copenhagen` | The whole-run replay of Copenhagen, which takes as long as the other four scenes together |
| `test_visual_polish_smoke_signals` | `signals` | The signal head checks |
| `test_visual_polish_smoke_stations` | `stations` | The station overlays of the five scenes, and of Copenhagen at a device pixel ratio of 2 |
| none | `scripts` | `scene_render_smoke.sh` and `legacy_import_smoke.sh`, which are tests of their own |

The script without arguments runs all phases in the order of the table. The other GUI smokes are CTest tests of
their own, `test_scene_render_smoke`, `test_track_preview_smoke`, `test_legacy_import_smoke`
and `test_route_diagram_smoke` (see [Platform coverage](testing.md#platform-coverage)), and
`ctest --test-dir build -R <name> --output-on-failure` starts one. CTest gives these
tests and the five visual tests a settings folder below `build/settings` and an output
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
none) and also runs Paimpol and Assignment.

The replay row is checked in the Assignment run of the script, in the same window code as
the other replay checks. `E2E_REPLAY_ROW_WHOLE_OK`: the slider covers 0 to the last time, the
last time is the last timestep of the run, the text below the controls says that the whole run
is kept and from where to where, Start goes to 0 s, the label beside the slider names the
selected frame as a clock time, and the width of the slider and the height of the row are the
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
frames, the last time and the accounted bytes and budget. The `replay` and `replay-copenhagen` phases of
`visual_polish_smoke.sh` do so for all six committed scenes, so a history that drops the first part of one of them, or
ends before the last timestep of the run, fails the script. No committed scene is larger
than the budget at its own horizon, which is why the dropped row is built from a smaller
budget in the Assignment run.

The explanation of Follow is checked by several smoke scripts. `app/FollowAvailability.h`
decides whether the control is offered, whether Follow can be switched on, whether it has
to be switched off, whether the view and the station emphasis follow the train, whether
the view glides to the train (a live run) or cuts to it (a replay) and which clause and
sentence are shown; the window writes the answer into the train list, the status label and the
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
other Bash smokes (the paragraph below the table [Platform coverage](testing.md#platform-coverage) gives the defaults)
and prints `creator preview infrastructure smoke passed`. CTest runs the script only
without the option, as `test_creator_acceptance_smoke`, which checks the items of the
preview but does not compare them with a run.

## Documentation pictures

```bash
bash tools/docs/capture_screenshots.sh
```

The pictures in `docs/images` come from this script. It starts a build made with `EGTRAIN_BUILD_TESTS=ON`, where
the capture phase in `EGTRAIN/QEGTRAIN/tests/e2e/Captures.cpp` opens each case, puts the window or dialog in the
state of the picture and saves it. Windows are 1200 by 750 pixels and dialogs keep their own size. The pictures show
the look of the platform, so take them on macOS with a screen at a device pixel ratio of 1, such as a macOS virtual
machine with a display that is not HiDPI: on a Retina screen the capture phase stops with a message that names the
ratio. The window needs a screen on which a 1200 by 750 window fits. The script writes the pictures and
`docs/images/captures.json`, which gives the case, state, size and program version of each. A picture that cannot be
scripted, such as the README picture with a run on screen, is added to `captures.json` by hand, with `"manual"`
holding the steps to take it, and the script keeps such entries. `--check` takes the pictures into a temporary folder
and lists the ones that are missing, have another size or differ in more than 2 percent of their pixels; a pixel
within 32 of the reference in each colour channel, at its place or next to it, counts as unchanged. The release
checklist runs it. `--taken` takes the pictures on the offscreen platform and lists only the missing ones, because a
dialog takes its size from the font and style of the platform. CTest runs it on macOS as `test_capture_screenshots`.
`check_docs.py` fails when a picture in `docs/images` and `captures.json` disagree.

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

## Where test and run output goes

| Output | Where | Set by |
| --- | --- | --- |
| Result of a CTest run | `build/Testing/Temporary/LastTest.log`; the names of the failed tests are in `LastTestsFailed.log` in the same folder. `ctest --rerun-failed --output-on-failure` runs those again, and `--output-log FILE` writes the log to a file | CTest |
| Files of a run in the application or from `QEGTRAIN` | `<QStandardPaths::AppDataLocation>/Output/<scene>`; the files are in [Run output](../guides/run-output.md) | `QEGTRAIN_OUTPUT_DIR` replaces the base folder, and `File > Set Output Folder...` replaces the folder of every later run in that session ([Output folder](../guides/command-line.md#output-folder)) |
| Messages of a run | Standard output and error, the **Console Log** dock, and the file named by `EGTRAIN_LOG_FILE` | `EGTRAIN_LOG`, `EGTRAIN_LOG_FILE` ([Log output](../guides/command-line.md#log-output)) |
| User settings of a test that starts the window | `build/settings/<test>` | `QEGTRAIN_E2E_SETTINGS_DIR`, set by CMake |
| Run output of the scene render, track preview, legacy import, route diagram and visual polish tests | `build/output/<test>`. `test_scene_drop` and `test_output_folder` write to `build/scene-drop-test` and `build/output-folder-test` | `QEGTRAIN_OUTPUT_DIR`, set by CMake |
| Logs and screenshots of the Bash smokes | `${TMPDIR:-/tmp}`, for example `qegtrain-visual-polish-e2e.log`; see [Smoke artifacts](#smoke-artifacts) | `TMPDIR` (`TEMP` and `TMP` on Windows) |
| Output of `tools/e2e/headless_smoke.py` | `EGTRAIN/QEGTRAIN/Output/<scene>` and `tools/e2e/headless_case_<n>.log`. Git ignores both. The script sets `QEGTRAIN_OUTPUT_DIR` itself | The case numbers on the command line; none runs all six, and Copenhagen alone takes minutes. CTest runs each case as its own test |
| Files of `tools/e2e/roundtrip_smoke.py` | A temporary folder that the script removes when it ends | |
| Unsaved-work copies that the window keeps | A `Recovery` folder in the local application data folder | `QEGTRAIN_RECOVERY_DIR` ([Scene editor flow](../architecture/scene-editor-flow.md#window-flow)) |

Point `TMPDIR` at a folder outside the checkout when several people test on one machine, so that
no run reads another run's files. Do the same with `QEGTRAIN_OUTPUT_DIR` for manual runs of the
application; CTest and the smoke scripts set it themselves.
