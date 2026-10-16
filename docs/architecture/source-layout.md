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
installer/                Inno Setup script of the Windows installer and its README
tools/e2e/                Smoke tests and Python tests
tools/release/            Version, package check, release asset and disk image scripts
tools/memory/             Peak-memory measurement and the ownership inventory
tools/performance/        Startup timing and playback profiling scripts
tools/golden_master/      Token-wise comparison of output files against a baseline directory
tools/format.py           Checks or applies the code format of the C++ sources
tools/docs/               Checks the Markdown documentation
docs/                     Guides and architecture, development, product, telemetry and UI documentation
.github/workflows/        GitHub Actions workflows: build and test, format, packages, releases and the telemetry contract check
```

## Folders

| Folder | Holds |
| --- | --- |
| `app/` | Entry point and the top-level window: `main.cpp`, `MainWindow`, `DispatchController`, `SceneOpenRequest`, `SceneOutputDirectory`, `OpenRequestRelay`, `resources.qrc` |
| `simulation/` | The simulation engine and domain: `Simulation`, `SimulationWorker`, `RunControl`, `RollingStock`, `RollingStockBlockingTimes`, `RollingStockBraking`, `RollingStockDynamics`, `RollingStockOccupation`, `RollingStockStops`, `RollingStockTrajectory`, `Infrastructure`, `Signalling`, `Optimisation`, `Passengers`, `NumberGenerator`, `SimulationRun`, `InitialParameters`, `RuntimeLimits` |
| `simulation/builders/` | The builders that fill the runtime globals from a scene: `InfrastructureBuilder` (infrastructure and signalling) and `OperationsBuilder` (trains, platforms and passengers), and `InfrastructureBuilderInternal.h` for the two declarations the first shares with `Signalling.cpp` |
| `scene/` | The canonical scene model: `SceneModel`, `SceneImporter`, `SceneExporter`, `SceneValidator`, `SceneWriter`, `SceneBundle`, `SceneCompatibility`, `SceneMigration`, `SceneDiagnostic`, `SceneFields`, `SceneFieldReader`, `SceneHistory`, `SceneIds`, `SceneReferences`, `SectionInventory`, `SignallingLevel`, `SignallingLevelNames`, `StagedDirectory`, `StopInsertion`, `TrackPreview`, `Utf8Path`, `SceneTool` |
| `editors/` | The scene editors: `SceneDocument`, the model of the open scene with its dirty state and revision, `SceneEditorPanel`, the interface of an editor panel, `CaseSettingsPanel`, the Case Settings editor, `CompositionPanel`, the Compositions editor, `TrainUnitPanel`, the Rolling stock units editor, and `TrainUnitSourceLinker`, the links from rolling stock units to the files they came from |
| `graphics/` | The network canvas (`NetworkPresenter`), and view (`NetworkScene`, `NetworkView`), the item data roles track preview items on registries of a signal (`SignalItemRoles`), visual style tables (`VisualPolish`) |
| `graphics/items/` | The `QGraphicsItem` subclasses that draw the network (see the rename table) |
| `widgets/` | Dock widgets and small controls: `ConsoleWidget`, `InfoDockWidget`, `TimeProgressBar`, `AboutDialog`, `CaseChooserDialog`, `NetworkLegendWidget`, `ColorChoiceButton`, `ChoiceComboBox`, `CompactDoubleSpinBox`, `DialogLayout`, `EditorLayout`, `ElidedLabel`, `UserPrompts` |
| `diagrams/` | Chart and result windows: `DiagramWindow`, `BlockingTimeDiagram`, `BlockingTimeScope`, `CapacityAnalysis`, `CapacityScope`, `AreaCapacity`, `AreaCapacityWindow`, `CompletedRunData`, `CompletedRunCapture`, `RunExports`, `RunResults`, `TimetableTableWindow`, `RouteDiagramCoordinates`, `RouteReferenceChoice`, `TractionCurve`, `TrainColors`, `TrainFilterButton`, `SimulationHeaders` |
| `io/` | Interoperability formats: `RttpXml`, the XML of the legacy channels, and `ExternalState`, the sender of that XML; `LiveFrame`, the JSON frames of the live contract (`docs/live/frame-v1.schema.json`), and `LiveServer`, the ZeroMQ transport that publishes them; vendored pugixml in `io/third_party/` |
| `util/` | Cross-cutting helpers and the logger: `Util`, `timeutil`, `TrajectoryUtil`, `portability`, `QtMsvcCompat`, `Log`, `Logger`, `CsvWriter`, `PlaybackProfiler`, `Version`, `SpeedFormat`, `TimeFormat` |
| `update/` | Release check, package download and self-update: `UpdateChecker`, `HttpTransport`, `WinHttpTransport`, `ReleaseInfo`, `Distribution`, `SelfUpdater`, `UpdatePreparation`, `UpdateSettings`, `UpdateRecord`, `UpdateNotices`, and `UpdateHelper`, the source of the `egtrain_update_helper` executable |
| `telemetry/` | Consent, queue and sender for usage and diagnostics events: `TelemetryConsent`, `TelemetryConsentDialog`, `TelemetryEvent`, `TelemetryOperation`, `TelemetryQueue`, `TelemetrySender` |
| `recovery/` | `RecoveryStore`: recovery copies of a scene with unsaved edits, kept below a folder that the caller names. `RecoveryController`: writes them on a timer on one worker thread. `RecoveryPrompt`: the dialog that offers the copies that an earlier run left behind. It has nothing to do with the recovery time of a timetable |
| `tests/` | C++ regression tests (`test_*.cpp`), the `characterization/` cases, the `e2e/` runner of the scripted GUI scenarios and the test `fixtures/` |

## Completed-run data

When a run ends, `captureCompletedRun` (`diagrams/CompletedRunCapture.cpp`) copies what the result views need out of the
simulation into one `CompletedRunData`, and `MainWindow` holds it as `m_completedRun`, a
`std::shared_ptr<const CompletedRunData>`. The Run Results dock reads it, and every result window and CSV export keeps
its own pointer. So a window that was opened for one run goes on showing and exporting that run after another run has
replaced the simulation state. The code in `diagrams/` reads the simulation only in `CompletedRunCapture.cpp` and
`RunResults.cpp`. `tools/e2e/test_diagrams_no_sim_globals.py` fails when another file of `diagrams/`, or
`app/MainWindowSupport.h`, includes a header of `simulation/` or names `regional_train`, `train_route`, `numRegions` or
`initial_variables`.

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
| `egtrain_sim` | `simulation/` except `RunControl` and `SimulationWorker` | `egtrain_scene`, `egtrain_util` |
| `egtrain_util` | `util/*.cpp` | nlohmann-json |
| `egtrain_rttp` | `io/RttpXml.cpp`, `io/ExternalState.cpp` | `egtrain_pugixml`, cppzmq |
| `egtrain_pugixml` | `io/third_party/pugixml.cpp` | |
| `egtrain_live` | `io/LiveFrame.cpp`, `io/LiveServer.cpp` | nlohmann-json, `egtrain_util`, cppzmq |
| `egtrain_graphics` | `graphics/` | Qt Core, Gui and Widgets, `egtrain_scene`, `egtrain_util` |
| `egtrain_widgets` | `widgets/*.cpp` | Qt Core, Gui and Widgets, `egtrain_graphics`, `egtrain_util` |
| `egtrain_editors` | `editors/` | Qt Core, Gui and Widgets, `egtrain_scene`, `egtrain_diagrams` |
| `egtrain_http` | `update/HttpTransport`, and on Windows `WinHttpTransport` | Qt Core and Network, and on Windows WinHTTP and Crypt32 |
| `egtrain_update` | `update/Distribution`, `ReleaseInfo`, `UpdateChecker`, `UpdatePreparation`, `UpdateRecord`, `UpdateSettings` | Qt Core and Network, `egtrain_http`, `egtrain_util` |
| `egtrain_sceneopen` | `app/SceneOpenRequest.cpp` | Qt Core |
| `egtrain_openrelay` | `app/OpenRequestRelay.cpp` | Qt Core, Gui and Network |
| `egtrain_recovery` | `recovery/` | Qt Core, Gui and Widgets, `egtrain_scene` |
| `egtrain_telemetry` | `telemetry/` except `TelemetryConsentDialog` | Qt Core, Gui, Network and Widgets, `egtrain_http` |
| `egtrain_dispatch` | `app/DispatchController.cpp`, `app/GuiReplayHistory.cpp`, `simulation/RunControl.cpp`, `simulation/SimulationWorker.cpp` | Qt Core, Gui and Widgets, `egtrain_rttp`, `egtrain_sim` |
| `egtrain_diagrams` | `diagrams/` | Qt Core, Gui, Network, Widgets and Charts, `egtrain_sim` |

`egtrain_miniz` holds the vendored zip code. The first six libraries and
`scene_tool` use no Qt. The dependencies point one way; no library depends on `MainWindow`.
`egtrain_recovery` is linked by `test_recoverystore`, `test_recoverycontroller` and `test_recoveryprompt` only.

The update code and the telemetry sender make their HTTP requests through `HttpTransport`
(`update/HttpTransport.h`), a GET for the update code and a POST for the sender. The
function `createHttpTransport` selects the implementation that the application uses: Qt's network
manager, or on Windows `WinHttpTransport`, because Qt 5.15 has no TLS library there. The WinHTTP
transport fetches https addresses only. A test passes `allowLoopbackHttp` to reach `http://127.0.0.1`
as well. `UpdateChecker` can take any transport, so `test_updatechecker` gives it a fake one, and
`test_httptransport` runs the transport of the application, for a GET and a POST, against a server on the loopback address.
`test_updatetransport_https` sends one real request to the GitHub release list through that transport
([HTTPS update check](../development/build-and-test.md#https-update-check)).

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

Still compiled in more than one target: the telemetry sources of the hook flavours,
`TelemetryConsentDialog.cpp`, which the application builds without the consent hook
and `test_telemetryconsent` with it, and `SelfUpdater.cpp`, which `test_selfupdater`
builds with `EGTRAIN_UPDATE_TEST_HOOK`. The hook lets that test replace the
capability of the running copy and the base of the package URLs, and lets its transport
fetch over http, so that it can update from a server on the loopback address into a folder
of its own. Only the application
builds the rest of `app/` and `UpdateNotices`. When tests are built, `QEGTRAIN` also
compiles `tests/e2e`.

## Renames

The graphics classes were named after their Qt base type. They are now named
after what they draw.

| Old | New |
| --- | --- |
| `myQGraphicsScene` | `NetworkScene` |
| `myQGraphicsView` | `NetworkView` |
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
| `IOClass` (file) | `RttpXml` and `ExternalState` |
| `Owl` | `Logger` |
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
