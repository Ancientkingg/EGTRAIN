# Build And Test

## Requirements

- CMake 3.16 or newer
- C++17 compiler
- Qt 5 Core, Gui, Widgets, Charts, and Svg
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

Current tests cover time formatting, input validation, speed formatting, trajectory accessors, blocking-time diagram data, visual classification, scene validation, scene import/export, scene writing, native scene route building, and smoke output decoding.

Scene tests use CTest labels:

```bash
ctest --test-dir build -L scene-v1 --output-on-failure
ctest --test-dir build -L legacy-compat --output-on-failure
ctest --test-dir build -LE legacy-compat --output-on-failure
```

`scene-v1` covers the canonical scene model. `legacy-compat` covers direct legacy input and the converter boundary; it is separate from the main scene-model bracket but remains useful while the simulator runs converter-produced legacy files. Add a `scene-v2` label when V2 tests exist.

## Simulation Smoke Test

```bash
tools/e2e/headless_smoke.py
```

The smoke test runs Netherlands (`-n 1`), Paimpol (`-n 2`), Copenhagen (`-n 3`), and Brescia (`-n 4`). It checks clean exit for all four cases, plus changing train positions, valid trajectory samples, and non-empty served-station rows where those assertions are enabled.

## Scene Roundtrip Smoke Test

```bash
tools/e2e/roundtrip_smoke.py
```

The roundtrip smoke validates and exports all four committed scene directories, then runs the exported legacy inputs for cases 1 through 4.

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
tools/e2e/roundtrip_smoke.py
```

For scene-format changes, also validate and export all four scene directories with `build/scene_tool`. For UI or rendering changes, also run `tools/e2e/visual_polish_smoke.sh`.
