# QEGTRAIN source layout

The C++ sources live under `EGTRAIN/QEGTRAIN/`, grouped by responsibility.
Before the reorganization tracked in #91, all 108 sources sat in that one
directory with names taken from their Qt base class. This document records the
folders and the renames so the next reader does not have to reconstruct them.

## Repository layout

```text
EGTRAIN/QEGTRAIN/         C++ Qt application source; the folders are listed below
EGTRAIN/QEGTRAIN/Scenes/  The committed case studies
EGTRAIN/QEGTRAIN/tests/   C++ regression tests and their fixtures
tools/e2e/                Smoke tests and Python tests
tools/release/            Version and package check scripts
tools/memory/             Peak-memory measurement and the ownership inventory
tools/performance/        Startup timing and playback profiling scripts
tools/golden_master/      Token-wise comparison of output files against a baseline directory
tools/format.py           Checks or applies the code format of the C++ sources
docs/                     Guides and architecture, development, product, telemetry and UI documentation
.github/workflows/        GitHub Actions workflows: build and test, format, packages, releases and the telemetry contract check
```

## Folders

| Folder | Holds |
| --- | --- |
| `app/` | Entry point and the top-level window: `main.cpp`, `MainWindow`, `DispatchController`, `resources.qrc` |
| `simulation/` | The simulation engine and domain: `Simulation`, `SimulationWorker`, `RollingStock`, `Infrastructure`, `Signalling`, `Capacity`, `Optimisation`, `Passengers`, `NumberGenerator`, `DispatchDecision`, `InitialParameters` |
| `scene/` | The canonical scene model: `SceneModel`, `SceneImporter`, `SceneExporter`, `SceneValidator`, `SceneWriter`, `SceneDiagnostic`, `SceneTool` |
| `graphics/` | The network canvas and view (`NetworkScene`, `NetworkView`), the visual style tables (`VisualPolish`) |
| `graphics/items/` | The `QGraphicsItem` subclasses that draw the network (see the rename table) |
| `widgets/` | Dock widgets and small controls: `ConsoleWidget`, `InfoDockWidget`, `TimeProgressBar` |
| `diagrams/` | Chart windows: `DiagramWindow`, `BlockingTimeDiagram` |
| `io/` | Interoperability formats; vendored pugixml in `io/third_party/` |
| `util/` | Cross-cutting helpers and the logger: `Util`, `TimeUtil`, `TrajectoryUtil`, `portability`, `Logger`, `SpeedFormat`, `TimeFormat` |
| `update/` | Release check, package download and self-update: `UpdateChecker`, `ReleaseInfo`, `SelfUpdater`, `UpdatePreparation`, `UpdateSettings`, and `UpdateHelper`, the source of the `egtrain_update_helper` executable |
| `telemetry/` | Consent, queue and sender for usage and diagnostics events: `TelemetryConsent`, `TelemetryConsentDialog`, `TelemetryEvent`, `TelemetryOperation`, `TelemetryQueue`, `TelemetrySender` |
| `tests/` | Unit tests |

## Includes

Includes are path-qualified against the source root, for example:

```cpp
#include "graphics/items/NodeItem.h"
#include "simulation/Signalling.h"
```

`CMakeLists.txt` puts `SRC_DIR` (`EGTRAIN/QEGTRAIN`) on the include path with
`include_directories(${SRC_DIR})`, so those paths resolve for the main target,
`scene_tool`, and every test.

## Libraries

`CMakeLists.txt` builds the code that more than one target uses once, as static libraries.
The application, `scene_tool` and the tests link them.

| Library | Sources | Depends on |
| --- | --- | --- |
| `egtrain_scene` | `scene/` except `SceneTool.cpp` | nlohmann-json, `egtrain_miniz` |
| `egtrain_sim` | `simulation/` except `SimulationWorker` | `egtrain_scene`, `egtrain_util` |
| `egtrain_util` | `util/*.cpp` | nlohmann-json |
| `egtrain_railml` | `io/RailMLParser.cpp` | `egtrain_pugixml`, cppzmq |
| `egtrain_pugixml` | `io/third_party/pugixml.cpp` | |
| `egtrain_graphics` | `graphics/` | Qt Core, Gui and Widgets, `egtrain_scene`, `egtrain_util` |
| `egtrain_widgets` | `widgets/*.cpp` | Qt Core, Gui and Widgets, `egtrain_graphics`, `egtrain_util` |
| `egtrain_update` | `update/ReleaseInfo`, `UpdatePreparation`, `UpdateSettings` | Qt Core, `egtrain_util` |
| `egtrain_telemetry` | `telemetry/` except `TelemetryConsentDialog` | Qt Core, Gui, Network and Widgets |
| `egtrain_dispatch` | `app/DispatchController.cpp`, `app/GuiReplayHistory.cpp`, `simulation/SimulationWorker.cpp` | Qt Core, Gui and Widgets, `egtrain_railml`, `egtrain_sim` |
| `egtrain_diagrams` | `diagrams/` | Qt Core, Gui, Network, Widgets and Charts, `egtrain_sim` |

`egtrain_miniz` holds the vendored zip code. The first five libraries and
`scene_tool` use no Qt. The dependencies point one way; no library depends on `MainWindow`.

The telemetry sources also build with test hooks, which add members and a constructor
parameter to `TelemetrySender`, `TelemetryConsent` and `TelemetryQueue`. These flavours
compile the same sources again: `egtrain_telemetry_hooks` (all hooks) when tests are built,
and `egtrain_telemetry_smoke` (the sender hook) in the isolated telemetry smoke
configuration, for the application. A target links one telemetry library, never two.
`egtrain_diagrams` links no telemetry library, because its windows call only
`OperationObservation`, whose symbols do not depend on the hooks. In the isolated
telemetry smoke configuration it is built with the sender hook, as the rest of the
application is. The moc object of `egtrain_diagrams` references both windows, so a target
that uses `TrainFilterButton`, `DiagramWindow`, `TimetableTableWindow` or
`RouteReferenceDialog` also links one telemetry library.

Still compiled in more than one target: the telemetry sources of the hook flavours, and
`TelemetryConsentDialog.cpp`, which the application builds without the consent hook
and `test_telemetryconsent` with it. Only the application builds the rest of `app/`,
`UpdateChecker` and `SelfUpdater`.

## Renames

The graphics classes were named after their Qt base type. They are now named
after what they draw.

| Old | New |
| --- | --- |
| `myQGraphicsScene` | `NetworkScene` |
| `myQGraphicsView` | `NetworkView` |
| `myQGraphicsItem` | `BaseNetworkItem` |
| `myQGraphicsEllipseItem` | `NodeItem` |
| `myQGraphicsRectItem` | `StationNodeItem` |
| `myQGraphicsLineItem` | `TrackLineItem` |
| `myQGraphicsPixmapItem` | `IconItem` |
| `myQGraphicsColorizeEffect` | `HighlightEffect` |
| `connectionQGraphicsLineItem` | `ConnectionItem` |
| `passengerQGraphicsPixmapItem` | `PassengerItem` |
| `platformQGraphicsRectItem` | `PlatformItem` |
| `signallingQGraphicsEllipseItem` | `SignalItem` |
| `trainQGraphicsItemGroup` | `TrainItemGroup` |
| `trainQGraphicsPolygonItem` | `TrainBodyItem` |
| `virtualArcQGraphicsLineItem` | `VirtualArcItem` |
| `timeQProgressBar` | `TimeProgressBar` |
| `infoQDockWidget` | `InfoDockWidget` |
| `EGTRAIN` (class) | `DispatchController` |
| `IOClass` (file) | `RailMLParser` |
| `Owl` | `Logger` |
| `dispDecision` | `DispatchDecision` |
| `initial_parameters` | `InitialParameters` |

The `EGTRAIN` rename touched the class only. The project name, the `QEGTRAIN`
target, the `EGTRAIN_*` CMake options, and the `EGTRAIN` window title,
organization name, and home directory are unchanged.

The `Widget.{cpp,h,ui}` demo, a `QWidget` with one button that nothing
constructed, was removed.

## Build

```
cmake -S . -B build -DEGTRAIN_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```
