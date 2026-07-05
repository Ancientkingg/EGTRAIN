# QEGTRAIN source layout

The C++ sources live under `EGTRAIN/QEGTRAIN/`, grouped by responsibility.
Before the reorganization tracked in #91, all 108 sources sat in that one
directory with names taken from their Qt base class. This document records the
folders and the renames so the next reader does not have to reconstruct them.

## Folders

| Folder | Holds |
| --- | --- |
| `app/` | Entry point and the top-level window: `main.cpp`, `MainWindow`, `DispatchController`, `resources.qrc` |
| `simulation/` | The simulation engine and domain: `Simulation`, `SimulationWorker`, `RollingStock`, `Infrastructure`, `Signalling`, `Capacity`, `Optimisation`, `Rescheduling`, `Passengers`, `NumberGenerator`, `DispatchDecision`, `InitialParameters` |
| `scene/` | The canonical scene model: `SceneModel`, `SceneImporter`, `SceneExporter`, `SceneValidator`, `SceneWriter`, `SceneDiagnostic`, `SceneTool` |
| `graphics/` | The network canvas and view (`NetworkScene`, `NetworkView`), the visual style tables (`VisualPolish`) |
| `graphics/items/` | The `QGraphicsItem` subclasses that draw the network (see the rename table) |
| `widgets/` | Dock widgets and small controls: `ConsoleWidget`, `InfoDockWidget`, `TimeProgressBar` |
| `diagrams/` | Chart windows: `DiagramWindow`, `BlockingTimeDiagram` |
| `io/` | Input, formats, geocoding, validation; vendored pugixml in `io/third_party/` |
| `util/` | Cross-cutting helpers and the logger: `Util`, `TimeUtil`, `TrajectoryUtil`, `portability`, `Logger`, `SpeedFormat`, `TimeFormat` |
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
