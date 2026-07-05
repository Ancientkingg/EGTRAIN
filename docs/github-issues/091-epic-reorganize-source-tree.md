# #91 Epic: Reorganize The QEGTRAIN Source Tree And Retire Boilerplate Names

- GitHub: https://github.com/Ancientkingg/EGTRAIN/issues/91
- State: open
- Labels: `type: epic`, `refactor`
- Created: 2026-07-05
- Mirrored: 2026-07-05

## Summary

All 119 tracked C++ sources sit in one flat directory, `EGTRAIN/QEGTRAIN/`. Many files are named after the Qt base class they extend rather than the job they do: `myQGraphicsScene`, `myQGraphicsView`, `myQGraphicsEllipseItem`, `connectionQGraphicsLineItem`, `signallingQGraphicsEllipseItem`, and so on. The name `myQGraphicsEllipseItem` tells you it derives from `QGraphicsEllipseItem`; it does not tell you the item draws a network node.

This epic does two things and changes no behaviour:

1. Groups the sources into module folders (app, simulation, scene, graphics, widgets, diagrams, io, util).
2. Renames the boilerplate files and classes to describe what they do.

## Target layout

```
QEGTRAIN/
├── app/          main, MainWindow, DispatchController, resources.qrc
├── simulation/   Simulation, SimulationWorker, RollingStock, Infrastructure,
│                 Signalling, Capacity, Optimisation, Rescheduling, Passengers,
│                 NumberGenerator, DispatchDecision, InitialParameters
├── scene/        SceneModel, SceneImporter, SceneExporter, SceneValidator,
│                 SceneWriter, SceneDiagnostic, SceneTool
├── graphics/     NetworkScene, NetworkView, VisualPolish
│   └── items/    BaseNetworkItem, NodeItem, StationNodeItem, TrackLineItem,
│                 IconItem, HighlightEffect, ConnectionItem, PassengerItem,
│                 PlatformItem, SignalItem, TrainItemGroup, TrainBodyItem,
│                 VirtualArcItem
├── widgets/      ConsoleWidget, InfoDockWidget, TimeProgressBar
├── diagrams/     DiagramWindow, BlockingTimeDiagram
├── io/           RailMLParser, Geocoding, InputValidation, third_party/pugixml
├── util/         Util, TimeUtil, TrajectoryUtil, portability, Logger,
│                 SpeedFormat, TimeFormat
└── tests/        (unchanged, includes updated)
```

## Child issues

- #92 Group the flat QEGTRAIN sources into module folders
- #93 Rename the Qt graphics classes to describe what they draw
- #94 Rename the remaining vague classes: EGTRAIN, IOClass, Owl, dispDecision, initialParameters
- #95 Remove or repurpose the leftover Widget demo

## Constraints

- No behaviour change. The build and all 12 ctest targets pass after each child issue.
- Includes become path-qualified, for example `#include "graphics/items/NodeItem.h"`. `${SRC_DIR}` stays on the include path so they resolve without per-target include changes.
- Each child issue is one commit, so each step is reviewable and bisectable on its own.

## Acceptance criteria

- Sources are grouped under app/, simulation/, scene/, graphics/, widgets/, diagrams/, io/, util/.
- No file or class name begins with `my` or is named after its Qt base class.
- `cmake --build` succeeds and `ctest` reports 12/12 on macOS with Qt 5.

