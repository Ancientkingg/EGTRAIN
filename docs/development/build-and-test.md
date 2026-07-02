# Build And Test

## Requirements

- CMake 3.16 or newer
- C++17 compiler
- Qt 5 Core, Gui, Widgets, and Charts
- OpenMP runtime
- ZeroMQ, cppzmq, and nlohmann-json

## Configure

```bash
cmake -S . -B build -DEGTRAIN_BUILD_TESTS=ON
```

On macOS with Homebrew Qt 5:

```bash
brew install qt@5 libomp zeromq cppzmq nlohmann-json
cmake -S . -B build -DEGTRAIN_BUILD_TESTS=ON -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt@5
```

## Build

```bash
cmake --build build
```

## Unit Tests

```bash
ctest --test-dir build --output-on-failure
```

Current tests cover time formatting, input validation, speed formatting, trajectory accessors, blocking-time diagram data, and visual classification.

## Simulation Smoke Test

```bash
tools/e2e/headless_smoke.py
```

The smoke test runs Copenhagen (`-n 3`) and Brescia (`-n 4`). It checks clean exit, changing train positions, valid trajectory samples, and non-empty served-station rows.

## GUI Smoke Test

```bash
tools/e2e/visual_polish_smoke.sh
```

Run this after UI or rendering changes.

## Verification Gates

For UI changes:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
tools/e2e/visual_polish_smoke.sh
```

For simulation, scene model, data conversion, or memory changes:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
tools/e2e/headless_smoke.py
tools/e2e/visual_polish_smoke.sh
```
