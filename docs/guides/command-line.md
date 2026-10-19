# Command-line options and scene_tool

This page is for scripts, headless runs and developers; you do not need it to run EGTRAIN from a downloaded package.

For the executable paths of a local build see [Run a local build](../development/build-and-test.md#run-a-local-build), and for how the Windows program behaves in a terminal see [Windows headless runs](../development/build-and-test.md#windows-headless-runs).

## Options

| Option | Value | Default | Effect |
| --- | --- | --- | --- |
| `-n` | 1 to 6 | 1 | Selects the case study; see [Case study numbers](#case-study-numbers). Any other value stops the start with exit code 1 and the message `ERROR: Unknown case study id`, followed by the valid ids, also when `--scene` is given. |
| `--scene` | a scene folder or an `.egscene` file | the folder that `-n` selects | Decides which scene loads. A relative path is read from the folder the program is started in, not from the folder of the program; `-n` finds the example cases of the package. A missing value, or a value that starts with `-`, stops the start with exit code 1. |
| `-h` | seconds | `simulation_settings.duration_seconds` of the scene | The simulation horizon. It is not help. A value below 1 makes the scene fail the runnable check, so the start stops. |
| `-b` | seconds | `simulation_settings.buffer_time_seconds` of the scene, rounded to a whole number; 0 if the scene has none | Overrides the buffer time of the scene. |
| `-c` | percent | `simulation_settings.recovery_time_percent` of the scene, rounded to a whole number; 0 if the scene has none | Overrides the recovery time of the scene. |
| `-g` | 0 or 1 | 1 | `-g 0` runs without a window and exits when the run ends. Any other nonzero number counts as 1. |
| `-pax` | 0 or 1 | the last choice of **View > Show Passengers**, 0 at the first start | Read only when `-g` is 1; any nonzero number counts as 1. In the window, a run that starts with 1 creates the passenger markers, counters and platform bars; **View > Show Passengers** (the same switch as the Passengers check box in the Case and Layers dock) changes it while the program runs: off hides them at once, on shows them from the next run; the list of passengers waiting at a platform is updated at every time step in a run that starts with 1 and at the boarding step of a train in one that starts with 0; the provenance file records the value of the run as `pax_mode`. |
| `-TSM` | 0 or 1 | 0 | Legacy output, off by default; any nonzero number counts as on. Deprecated: it is kept for one release, a nonzero value prints a warning on standard error, and [`--live`](#options) replaces it. At every simulation step it tries to send the traffic state to a ZeroMQ peer on port 5555 of the local machine. The messages are unversioned and this is not a supported interface; see [External state sharing](../architecture/external-sharing.md). |
| `-RC` | 0 or 1 | 0 | Legacy output, off by default; any nonzero number counts as on. Deprecated in the same way as `-TSM`. At every simulation step it tries to send the passengers' route choice requests to a ZeroMQ peer on port 5556 of the local machine. The messages are unversioned and this is not a supported interface; see [External state sharing](../architecture/external-sharing.md). |
| `--interactive` (also `-interactive`) | none | off | Restores the legacy questions: asks on standard input for each of `-n`, `-g`, `-pax`, `-TSM` and `-RC` that the command line leaves out; `-pax` is asked only when the window is on. Without `--interactive` the defaults of this table apply. |
| `--detailed-trajectories` | none | off | Also writes the per-train files in `TEMP` and `TrainTrajectories/TrainPathDiagram.txt`; see [Run output](run-output.md#detailed-trajectories). |
| `--live` | a port from 0 to 65535 | off | Publishes the state of every run of the process to a ZeroMQ socket on `127.0.0.1`; 0 takes a free port. The endpoints go to `live-endpoint.json` in the [output folder](#output-folder), and the start prints them on standard error. A value that is not a port stops the start with exit code 1, and a port that is in use stops it with exit code 2. See [Live stream](../architecture/external-sharing.md#live-stream). |
| `--live-every` | a whole number from 1 | 1 | With `--live`: a state frame for every N-th step. Without `--live` it stops the start with exit code 1. |
| `--live-commands` | none | off | With `--live`: also binds the command socket and writes the access token to `live-endpoint.json`. A client can then pause, resume and stop the run and set its step delay; see [Commands](../architecture/external-sharing.md#commands). Without `--live` it stops the start with exit code 1. |
| `--seed` | a whole number from 1 to 2147483646 | 789350715 | Sets the seed of the random draws; see [Random seed](#random-seed). Any other value stops the start with `ERROR: --seed requires a whole number from 1 to 2147483646.` and exit code 1. |

The values of `-n`, `-h`, `-g`, `-pax`, `-TSM`, `-RC`, `--live` and `--live-every` are read as whole numbers, those of `-b` and `-c` as decimal numbers. There is no `--help`. An unknown option is ignored, but the word after it is read as a file name.

A file name without an option opens that file, as `--scene` does. Exactly one `.egscene` file is accepted. A file of another kind, a file that does not exist, a directory and several files are reported and not opened: a window launch shows the message **Cannot Open Case Study**, and a headless launch prints `ERROR: Cannot open ...` and exits with 1. A bundle that does not load is reported the same way in a window launch; a headless launch prints the scene diagnostics and exits with 1, as it does for `--scene`. A window launch then starts as it does without a file name. When `--scene` is also given, `--scene` wins and the file name is ignored; a file name that opens wins over `-n`. The message box is not shown when `QEGTRAIN_DISABLE_UPDATES` or a test variable such as `QEGTRAIN_AUTOSTART` is set, but the `ERROR` line on standard error is always printed. A file name that starts with `-` is read as an option.

Examples, where `QEGTRAIN` stands for the executable of your build or package:

```bash
QEGTRAIN -n 3 -h 8000 -g 1 -pax 0 -TSM 0 -RC 0
QEGTRAIN --scene path/to/case.egscene
QEGTRAIN path/to/case.egscene
QEGTRAIN --interactive
QEGTRAIN --seed 789350715
QEGTRAIN -n 2 -g 0 --detailed-trajectories
QEGTRAIN -n 2 -g 0 --live 0 --live-every 10
```

## Case study numbers

Each number selects the scene folder of that name in `EGTRAIN/QEGTRAIN/Scenes`.

| `-n` | Scene folder |
| --- | --- |
| 1 | `Netherlands` |
| 2 | `Paimpol` |
| 3 | `Copenhagen` |
| 4 | `Milano_Brescia` |
| 5 | `Assignment_Gvc_Gdg_Ut` |
| 6 | `Lebanon` |

The scene folder `Amsterdam_Hilversum_Student` has no `-n` number. Start it with `--scene`, for example `--scene Scenes/Amsterdam_Hilversum_Student` from `EGTRAIN/QEGTRAIN`. The [student case guide](amsterdam-hilversum-student-case.md) describes the exercise.

## Startup of the window

With neither `-n` nor `--scene`, and no file name that opens, the window opens the **Open a Case** window over the case that the program has already loaded: Netherlands, or the case whose number you typed when `--interactive` asked for it. With `-n`, `--scene` or a file name that opens it does not open that window. A file name that does not open shows the message **Cannot Open Case Study** and the program then starts as without a file name: it loads the default case, or the case of `-n`, and opens the **Open a Case** window unless `-n` is given.

A launch whose only argument is a `.egscene` file, as when the system opens a file with EGTRAIN, hands the file to a window of the same copy of EGTRAIN that is already open and ends with exit code 0. That window opens the file as **File > Open Case Study** does, including the question about unsaved changes, and comes to the front. A dialog that is open, such as that question or the first-run questions, holds the file back until it is closed, and the **Open a Case** window closes first. Only one window listens at a time, and it takes files only from its own copy, so a launch of a copy in another folder opens the file in a window of its own. A launch with other arguments, with `-g 0` or in a test run starts as described above. Two launches of the Linux AppImage count as different copies, because each runs from its own mount folder. When no window answers within 5 seconds, the launch opens the file itself.

`-n` and the default case find their scene folder by name: first in `Scenes` next to the executable, then in `../Resources/Scenes` (the macOS app), `../share/EGTRAIN/Scenes` and `../../EGTRAIN/QEGTRAIN/Scenes`, each relative to the folder of the executable; an error in the scene stops the start (see [Exit codes](#exit-codes)).

## Random seed

`--seed` sets the seed of the random draws of a run: the passenger time windows and the passenger-dependent dwell times. Runs of the same scene with the same seed give the same results. The seed is recorded as `random_seed` in the provenance file that is saved next to exported results, named `<exported file>.provenance.json`.

## Output folder

By default EGTRAIN writes its output to `<Qt AppDataLocation>/Output/<scene>`, where `<scene>` is the `name` of the scene. `QEGTRAIN_OUTPUT_DIR` replaces the base directory, and EGTRAIN then writes `<that directory>/Output/<scene>`. A run of the case that is open in the window writes to the folder of that case under its present name, not to the folder of the case that the program started with. **File > Set Output Folder...** replaces the folder of every later run in that session, whichever case is open. [Run a local build](../development/build-and-test.md#run-a-local-build) states the same rule. [Run output](run-output.md) lists the files of a run.

```bash
QEGTRAIN_OUTPUT_DIR=/tmp/egtrain-run ../../build/QEGTRAIN --scene path/to/scene
```

## Log output

The warnings and events of the simulation and every Qt message go through the logging core, and each has a category and a level. The categories are `sim.setup`, `sim.step`, `sim.events`, `sim.pax`, `io.railml`, `io.zmq`, `app.run`, `app.ui` and `update`; the levels are `debug`, `info`, `warning` and `error`. A Qt message is an `app.ui` message at the level of its kind (`qDebug`, `qInfo`, `qWarning`, `qCritical`). Every category and level is on by default, except `sim.step` at `debug`. That is the line `At time <t> Train<name> is stopping at <station> at <platform>`, which a stopped train writes in every step. The progress of a run (`Time of simulation is <t>`, once in 100 steps) and the XML of the legacy `-TSM` and `-RC` channels go through the core too. Output that does not go through the core is not changed by the variables below: the line `Graphical user interface (GUI): 0` or `1`, the line `End of Simulation` that a run without a window prints when it completes, and the diagnostics of the scene.

`EGTRAIN_LOG` switches categories and levels on and off. It holds rules separated by `;` or a newline, each `pattern=true` or `pattern=false`. A pattern is a category name, a name with a `*` at the start, at the end or at both ends (`sim.*`, `*.run`, `*step*`), or `*`, with an optional level suffix (`app.ui.warning`); a later rule wins. `QT_LOGGING_RULES` still decides which Qt messages exist.

```bash
EGTRAIN_LOG='app.ui.debug=false' QEGTRAIN -n 2
```

To get the line of every stopped train back, set `EGTRAIN_LOG=sim.step=true`.

A malformed rule is skipped and the other rules apply. The program prints one `WARNING: EGTRAIN_LOG has a malformed rule` line on standard error and starts as usual; the exit code does not change.

`EGTRAIN_LOG_FILE` names a file. The program appends every message that goes through the core to it, as `level category: text` (`warning app.ui: text`), each message on a line of its own, without a limit on its size. When the file cannot be opened, the program prints a `WARNING` line on standard error and writes no file.

The Console Log dock (**View > Console Log**, Ctrl+Shift+L) shows a message with its level and category in front, such as `warning app.ui: text`. It shows all other lines as they are written. Standard output and standard error carry the text of a message without the level and category.

## Exit codes

- 0: a run without a window that finished, or a launch that handed its file to an open window.
- 1: a bad argument (also a `--live` option that is not valid), a scene with errors, or, without a window, a run in which no train was loaded. With a window, a scene with errors shows the message box **Cannot Start EGTRAIN** and the program exits with 1. A launch without a window with a file name that is rejected or does not load also exits with 1. A scene that was saved with a newer EGTRAIN, with an older unsupported scene format or in the draft layout of the builds before 2026-08-08 is not loaded: the program prints the reason on standard error and exits with 1. With a window, `--scene` hands such a scene to the window, which shows the matching dialog. A launch with a window and a file name that does not open is not an error exit: the program starts as without a file name.
- 2: the live stream that `--live` asks for cannot start, for example because its port is in use. The start stops before a run, and the reason is on standard error; a window launch also shows it in a message box.

## scene_tool

`scene_tool` works on scenes without the application. Run without arguments, it prints its usage and exits with 1:

```text
Usage:
  scene_tool import <legacyDir> <sceneDir> [sceneName]
  scene_tool pack <scene-directory> <output.egscene>
  scene_tool unpack <input.egscene> <output-directory>
  scene_tool export <scene-path> <outDir>
  scene_tool validate [--runnable] <scene-path>
```

| Command | Effect |
| --- | --- |
| `import` | Converts the legacy case in `<legacyDir>` into the scene `<sceneDir>`. Without `[sceneName]`, the name is the last part of `<legacyDir>`. |
| `pack` | Writes a scene folder as an `.egscene` file. |
| `unpack` | Writes the contents of an `.egscene` file to a directory. |
| `export` | Writes legacy files for a scene into `<outDir>`. |
| `validate` | Checks a scene folder or an `.egscene` file and prints the diagnostics. With `--runnable` it also applies the runnable check that the application makes before it starts a run. |

Each command exits with 0 on success and 1 otherwise. There is no `migrate` command.

The macOS and Windows packages contain `scene_tool`: `QEGTRAIN.app/Contents/MacOS/scene_tool` inside the macOS app, and `scene_tool.exe` next to the application in the extracted or installed Windows folder. A build from source also produces it; see [Build And Test](../development/build-and-test.md).

[Opening an `.egscene` case study](opening-a-case-study.md#instructor-and-command-line-use) has examples of `pack`, `validate` and `unpack`, including the rule that the unpack destination must be a new path.
