# EGTRAIN

EGTRAIN is a Qt desktop application for railway micro-simulation. It loads a case-study railway network, runs train movements over signalled infrastructure, and shows timetable, delay, speed, and blocking-time outputs.

The product goal is a student-friendly replacement for the current OpenTrack assignment workflow. The first target is a V1 workflow where students can load an assignment scene, edit trains and services inside the app, run simulations, inspect diagrams, and export report data without editing legacy text files by hand.

## Repository layout

```text
EGTRAIN/QEGTRAIN/        C++ Qt application source
EGTRAIN/QEGTRAIN/Input/  Case-study input data used by smoke tests
EGTRAIN/QEGTRAIN/tests/  C++ regression tests
tools/e2e/               End-to-end smoke tests
tools/golden_master/     Output comparison helpers
docs/                    Product, architecture, planning, build notes
```

Runtime output is written under `EGTRAIN/QEGTRAIN/Output/` and is ignored by git.

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

## Test

```bash
ctest --test-dir build --output-on-failure
tools/e2e/headless_smoke.py
tools/e2e/visual_polish_smoke.sh
```

The headless smoke test runs the Copenhagen and Brescia cases. It checks process exit, train movement, trajectory samples, and served-station output.

## Run

Run from `EGTRAIN/QEGTRAIN` so the legacy relative input paths resolve.

```bash
cd EGTRAIN/QEGTRAIN
../../build/QEGTRAIN.app/Contents/MacOS/QEGTRAIN -n 3
```

Case selector:

- `-n 1`: Netherlands
- `-n 2`: Paimpol
- `-n 3`: Copenhagen
- `-n 4`: Brescia

Copenhagen and Brescia are the current regression cases. The Netherlands data needs repair before it can serve as the assignment scenario.

## Planning

- [Assignment workflow](docs/product/assignment-workflow.md)
- [Scene model design](docs/architecture/scene-model.md)
- [Roadmap](docs/planning/roadmap.md)
- [Agile workflow](docs/planning/agile-workflow.md)
- [GitHub issue plan](docs/planning/github-issues.md)
- [Build and test guide](docs/development/build-and-test.md)
