# EGTRAIN

EGTRAIN is a desktop application for microscopic railway simulation. It
combines railway infrastructure, signalling, rolling stock, services,
timetables, and passenger demand in an interactive Qt interface.

The application is built for students and railway researchers who want to
create and study operating scenarios without editing collections of legacy
text files by hand. A complete workflow stays inside EGTRAIN: open a case
study, inspect or edit its trains and services, run the simulation, examine
the diagrams, and export data for reports or further analysis.

## What EGTRAIN does

- Loads railway networks and service data from the included case studies.
- Displays tracks, stations, signals, routes, and moving trains in a graphical
  scene.
- Lets users inspect and edit trains, services, timetables, and scene
  properties.
- Simulates train movement over signalled infrastructure, including delays and
  passenger operations.
- Produces timetable, train-path, delay, speed, trajectory, and blocking-time
  results.
- Exports simulation data for reports and external analysis.
- Retains compatibility with existing EGTRAIN input data while scene-based
  editing replaces manual text-file work.

## Background

EGTRAIN was originally developed by
[Egidio Quaglietta](https://orcid.org/0000-0002-7936-5832) as a microscopic
railway simulation model. The model and its early applications are described
in [Quaglietta's doctoral thesis](https://doi.org/10.6092/unina/fedoa/8599).

This repository continues that work as a desktop application for students and
researchers. The original EGTRAIN case-study inputs for the SORTEDMOBILITY
research project are available from
[4TU.ResearchData](https://doi.org/10.4121/e78d0dc2-3123-4510-a2c5-7ad017a02e33.v1).

## Included case studies

EGTRAIN includes four railway scenarios:

- Netherlands
- Paimpol, France
- Copenhagen, Denmark
- Milan to Brescia, Italy

They can be selected with the `-n` command-line option:

- `-n 1`: Netherlands
- `-n 2`: Paimpol
- `-n 3`: Copenhagen
- `-n 4`: Milan to Brescia

## Build

Requirements:

- CMake 3.16 or newer
- C++17 compiler
- Qt 5 Core, Gui, Widgets, and Charts
- OpenMP runtime
- ZeroMQ, cppzmq, and nlohmann-json

Configure and build:

```bash
cmake -S . -B build -DEGTRAIN_BUILD_TESTS=ON
cmake --build build
```

On macOS with Homebrew Qt 5:

```bash
brew install qt@5 libomp zeromq cppzmq nlohmann-json
cmake -S . -B build -DEGTRAIN_BUILD_TESTS=ON -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt@5
cmake --build build
```

## Run

Run EGTRAIN from `EGTRAIN/QEGTRAIN` so legacy relative input paths resolve:

```bash
cd EGTRAIN/QEGTRAIN
../../build/QEGTRAIN.app/Contents/MacOS/QEGTRAIN
```

Launching without arguments opens the graphical application with the
Netherlands case study. Command-line options can select another case or
configure an automated run. For example:

```bash
../../build/QEGTRAIN.app/Contents/MacOS/QEGTRAIN \
  -n 3 -h 8000 -g 1 -pax 0 -TSM 0 -RC 0
```

Add `--interactive` to use the legacy terminal questionnaire.

## Test

Run these commands from the repository root:

```bash
ctest --test-dir build --output-on-failure
tools/e2e/headless_smoke.py
tools/e2e/visual_polish_smoke.sh
```

The smoke tests cover all four case studies and check application startup,
train movement, trajectory samples, served-station output, and the graphical
interface.

## Documentation

- [Application and scene guide](docs/guides/scenes-and-application.md)
- [Scene property reference](docs/guides/v1-scene-properties.md)
- [Netherlands assignment guide](docs/guides/netherlands-assignment.md)
- [Assignment workflow](docs/product/assignment-workflow.md)
- [Scene model architecture](docs/architecture/scene-model.md)
- [Build and test guide](docs/development/build-and-test.md)

## Repository layout

```text
EGTRAIN/QEGTRAIN/        C++ Qt application source
EGTRAIN/QEGTRAIN/Input/  Included case-study data
EGTRAIN/QEGTRAIN/tests/  C++ regression tests
tools/e2e/               End-to-end smoke tests
tools/golden_master/     Output comparison helpers
docs/                    User, architecture, and development documentation
```

Runtime output is written under `EGTRAIN/QEGTRAIN/Output/` and is ignored by git.
