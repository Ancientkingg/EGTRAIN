# Command-line options and scene_tool

This page is for scripts, headless runs and developers; you do not need it to run EGTRAIN from a downloaded package.

For the executable paths of a local build see [Run a local build](../development/build-and-test.md#run-a-local-build), and for how the Windows program behaves in a terminal see [Windows headless runs](../development/build-and-test.md#windows-headless-runs).

## Options

| Option | Value | Default | Effect |
| --- | --- | --- | --- |
| `-n` | 1 to 6 | 1 | Selects the case study; see [Case study numbers](#case-study-numbers). Any other value stops the start with exit code 1 and the message `ERROR: Unknown case study id`, followed by the valid ids, also when `--scene` is given. |
| `--scene` | a scene folder or an `.egscene` file | the folder that `-n` selects | Decides which scene loads. A missing value, or a value that starts with `-`, stops the start with exit code 1. |
| `-h` | seconds | `simulation_settings.duration_seconds` of the scene | The simulation horizon. It is not help. A value below 1 makes the scene fail the runnable check, so the start stops. |
| `-b` | seconds | `simulation_settings.buffer_time_seconds` of the scene, rounded to a whole number; 0 if the scene has none | Overrides the buffer time of the scene. |
| `-c` | percent | `simulation_settings.recovery_time_percent` of the scene, rounded to a whole number; 0 if the scene has none | Overrides the recovery time of the scene. |
| `-g` | 0 or 1 | 1 | `-g 0` runs without a window and exits when the run ends. Any other nonzero number counts as 1. |
| `-pax` | 0 or 1 | 0 | Read only when `-g` is 1; any nonzero number counts as 1. In the window, `-pax 1` creates the passenger markers, counters and platform bars, so the Passengers layer in the Case and Layers dock has nothing to show without it; the list of passengers waiting at a platform is updated at every time step with `-pax 1` and at the boarding step of a train without it; the provenance file records the value as `pax_mode`. |
| `-TSM` | 0 or 1 | 0 | Legacy output, off by default; any nonzero number counts as on. At every simulation step it sends the traffic state to a ZeroMQ peer on port 5555 of the local machine. The messages are unversioned and this is not a supported interface; see [External state sharing (legacy)](../architecture/external-sharing.md). |
| `-RC` | 0 or 1 | 0 | Legacy output, off by default; any nonzero number counts as on. At every simulation step it sends the passengers' route choice requests to a ZeroMQ peer on port 5556 of the local machine. The messages are unversioned and this is not a supported interface; see [External state sharing (legacy)](../architecture/external-sharing.md). |
| `--interactive` (also `-interactive`) | none | off | Restores the legacy questions: asks on standard input for each of `-n`, `-g`, `-pax`, `-TSM` and `-RC` that the command line leaves out; `-pax` is asked only when the window is on. Without `--interactive` the defaults of this table apply. |
| `--seed` | a whole number from 1 to 2147483646 | 789350715 | Sets the seed of the random draws; see [Random seed](#random-seed). Any other value stops the start with `ERROR: --seed requires a whole number from 1 to 2147483646.` and exit code 1. |

The values of `-n`, `-h`, `-g`, `-pax`, `-TSM` and `-RC` are read as whole numbers, those of `-b` and `-c` as decimal numbers. There is no `--help`, and an unknown option is ignored.

Examples, where `QEGTRAIN` stands for the executable of your build or package:

```bash
QEGTRAIN -n 3 -h 8000 -g 1 -pax 0 -TSM 0 -RC 0
QEGTRAIN --scene path/to/case.egscene
QEGTRAIN --interactive
QEGTRAIN --seed 789350715
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

With neither `-n` nor `--scene`, the window opens the **Open a Case** window over the case that the program has already loaded: Netherlands, or the case whose number you typed when `--interactive` asked for it. With `-n` or `--scene` it does not open that window.

`-n` and the default case find their scene folder by name: first in `Scenes` next to the executable, then in `../Resources/Scenes` (the macOS app), `../share/EGTRAIN/Scenes` and `../../EGTRAIN/QEGTRAIN/Scenes`, each relative to the folder of the executable; an error in the scene stops the start (see [Exit codes](#exit-codes)).

## Random seed

`--seed` sets the seed of the random draws of a run: the passenger time windows and the passenger-dependent dwell times. Runs of the same scene with the same seed give the same results. The seed is recorded as `random_seed` in the provenance file that is saved next to exported results, named `<exported file>.provenance.json`.

## Output folder

By default EGTRAIN writes its output to `<Qt AppDataLocation>/Output/<scene>`, where `<scene>` is the `name` of the scene. `QEGTRAIN_OUTPUT_DIR` replaces the base directory, and EGTRAIN then writes `<that directory>/Output/<scene>`. [Run a local build](../development/build-and-test.md#run-a-local-build) states the same rule.

```bash
QEGTRAIN_OUTPUT_DIR=/tmp/egtrain-run ../../build/QEGTRAIN --scene path/to/scene
```

## Exit codes

- 0: a run without a window that finished.
- 1: a bad argument, a scene with errors, or, without a window, a run in which no train was loaded. With a window, a scene with errors shows the message box **Cannot Start EGTRAIN** and the program exits with 1.

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

The macOS and Windows packages contain `scene_tool` next to the application: `QEGTRAIN-Lebanon/scene_tool` in the extracted macOS folder and `scene_tool.exe` in the extracted Windows folder. A build from source also produces it; see [Build And Test](../development/build-and-test.md).

[Opening an `.egscene` case study](opening-a-case-study.md#instructor-and-command-line-use) has examples of `pack`, `validate` and `unpack`, including the rule that the unpack destination must be a new path.
