# QEGTRAIN source layout

The C++ sources live under `EGTRAIN/QEGTRAIN/`, grouped by responsibility.
Before the reorganization tracked in #91, all 108 sources sat in that one
directory with names taken from their Qt base class. This document records the
folders and the renames so the next reader does not have to reconstruct them.
[Getting started as a developer](../development/getting-started.md) shows how to build the
application and how to find the code behind a screen.

## Repository layout

```text
EGTRAIN/QEGTRAIN/         C++ Qt application source; the folders are listed below
EGTRAIN/QEGTRAIN/Scenes/  The bundled cases
EGTRAIN/QEGTRAIN/tests/   C++ regression tests and their fixtures
installer/                Inno Setup script of the Windows installer and its README
tools/e2e/                Smoke tests and Python tests
tools/release/            Version, package check, release asset and disk image scripts
tools/memory/             Peak-memory measurement and the ownership inventory
tools/performance/        Startup timing and playback profiling scripts
tools/format.py           Checks or applies the code format of the C++ sources
tools/tidy.py             Runs clang-tidy on the C++ sources outside simulation/
tools/docs/               Checks the Markdown documentation
docs/                     Guides and architecture, development, product, telemetry and UI documentation
.github/workflows/        GitHub Actions workflows: build and test, format, tidy, packages, releases and the telemetry contract check
```

## Folders

Paths are relative to `EGTRAIN/QEGTRAIN/`. A file named without its extension has a `.h` and usually a `.cpp`.

| Folder | Holds |
| --- | --- |
| `app/` | The program and its main window. `main.cpp` reads the command line, loads and validates the scene and starts the window or a run without one. [`MainWindow`] is the window, with `MainWindowSupport.h` (constants it shares with the scripted runs) and `MainWindowTestHooks.h` (the entry points of those runs, empty in a normal run). [`DispatchController`] prepares a scene and runs it; [`GuiReplayHistory`] holds the frames kept for replay, [`ReplayCoverage`] the words of the replay row and [`FollowAvailability`] the rule for following a train. [`RunSelection`] holds the selected scenario and the service occurrences that a run leaves out. `SceneOpenRequest` checks the file that a command line asks to open, [`OpenRequestRelay`] hands such a file to a window that is already running, and `CannotOpenMessage` is the text shown when a case does not open. [`SceneSession`] holds where the open scene came from and was saved, and opens, creates, saves and imports a scene. [`UpdateTelemetryController`] holds the update checks, the self update, the privacy dialogs and the telemetry sender of the window. [`RunController`] holds the run review, the preparation, start and stop of a run, and the status, completed data and delay baseline that it leaves. [`ResultViews`] opens the windows that show the completed run (the per-train charts, the route, timetable, delay, blocking-time and capacity views), and closes them with the traction curve windows of the editors when another case replaces the open one; [`RunResultsDock`] is the Run Results dock with its table, exports and delay baseline buttons, and lists the result views in the order of its menu and the Diagrams menu. `SceneOutputDirectory` and `DialogFolders` choose folders, and `SceneSavePolicy` protects the installed scenes from being overwritten. `RuntimeGeometryFromRun` is the geometry source of the canvas that reads the run of `DispatchController`. `UpdatePromptText` and `ReleaseNotesText` hold the texts of the update message boxes, and `TelemetrySmokeSupport` the hooks of the isolated telemetry smoke. `resources.qrc` embeds `resources/` |
| `simulation/` | The simulation engine and domain: `Simulation`, [`SimulationRun`] (the infrastructure and routes that a scene builds), [`SimulationWorker`] and [`RunControl`] (the run on its own thread and its pause and stop), `RollingStock` with `RollingStockBlockingTimes`, `RollingStockBraking`, `RollingStockDynamics`, `RollingStockOccupation`, `RollingStockStops` and `RollingStockTrajectory`, `Infrastructure`, `Signalling`, `Optimisation`, `Passengers`, [`NumberGenerator`], [`InitialParameters`] (the options of a run) and [`GuiSimulationSnapshot`] (the state of one step that `DispatchController` hands to the window and the live stream) |
| `simulation/builders/` | The builders that fill a run from a scene: `InfrastructureBuilder` (infrastructure and signalling) and `OperationsBuilder` (trains, platforms and passengers) |
| `scene/` | The canonical scene model. [`SceneModel`] is the model and the loader of a scene folder; `SceneBundle` reads and writes `.egscene` files; `SceneWriter` saves a folder; `SceneImporter` and `SceneExporter` convert from and to the legacy files; `SceneValidator` checks a model; `SceneCompatibility` and `SceneMigration` decide which scenes open; [`SceneDiagnostic`] is a finding. Also `SceneFields`, [`SceneFieldReader`], `RuntimeLimits`, [`SceneHistory`] (Undo and Redo), `SceneIds`, `SceneReferences`, `SectionInventory`, `SignallingLevel`, `SignallingLevelNames`, [`StagedDirectory`], `StopInsertion`, `TrackPreview` and `Utf8Path`. `SceneTool.cpp` is the source of the `scene_tool` program |
| `editors/` | The scene editors. [`SceneDocument`] is the open scene with its dirty state and revision, and [`SceneEditorPanel`] the interface of a panel. The panels are [`CaseSettingsPanel`], [`CompositionPanel`], [`TrainUnitPanel`] (rolling stock units), [`ServicePanel`], [`TimetableStopsPanel`], [`PassengerPanel`], [`ScenarioPanel`], [`IncidentPanel`], [`EntranceDelayPanel`] and [`InfrastructurePanel`]. [`ValidationPanel`] holds the validation table, the loaded data tree and the validation status; it is not an editor. [`TrainUnitSourceLinker`] keeps the links from rolling stock units to the files they came from |
| `editors/infrastructure/` | The kinds of entity that `InfrastructurePanel` shows as tables. [`InfrastructureFacet`] is the interface of one kind, `ListFacet` and `SectionRowsFacet` are bases for facets over a list of the model, and the facets are `TopologyFacets` (tracks, nodes, arcs and connections), `BlockRouteFacets` (blocks, routes, block dependencies and single-track restrictions), `StationFacets` (stations, platforms and station boundaries) and `SignalFacets` (signals and signalling areas) |
| `graphics/` | The network canvas. [`NetworkPresenter`] draws the track preview and the items of a run and shows each step on them; [`RuntimeGeometrySource`] is the interface through which it reads the infrastructure of a run. [`NetworkScene`] and [`NetworkView`] are the scene and the view, [`FollowCamera`] moves the view after a train, [`SignalGeometry`] and `AnnotationPlacement` place signal heads and station labels, `SignalItemRoles` names the item data roles of a signal, and `VisualPolish` holds the visual style tables |
| `graphics/items/` | The `QGraphicsItem` classes that draw the network, named after what they draw (see [Renames](#renames)), and the items of the selection cue ([`SelectionCueItem`], [`HighlightEffect`]), the station overlay ([`StationOverlayItem`]) and the train badge ([`TrainBadgeItem`]) |
| `widgets/` | Dock widgets, dialogs and small controls: [`ConsoleWidget`], [`InfoDockWidget`], [`InspectorPanel`] (the inspector dock on the right, whose title names the selected item: the form of the selected item and the selection that the window marks), [`TimeProgressBar`], [`AboutDialog`], [`CaseChooserDialog`], `FindDialog`, [`NetworkLegendWidget`], [`ColorChoiceButton`], [`ChoiceComboBox`], [`CompactDoubleSpinBox`], `DialogLayout`, `EditorLayout`, [`ElidedLabel`] and [`UserPrompts`] |
| `diagrams/` | Result views and their data. `CompletedRunCapture` copies a finished run into [`CompletedRunData`]; [`RunResults`] builds the Run Results table; `RunExports` writes the CSV files. The windows are [`DiagramWindow`], [`TimetableTableWindow`], `BlockingTimeDiagram`, [`DelayAnalysisWindow`], `CapacityWindow` and [`AreaCapacityWindow`], over [`BlockingTimeScope`], `CapacityAnalysis`, `CapacityScope`, `AreaCapacity` and `DelayAnalysis`. Also `PositionScale` (route distance to geo X), `RouteDiagramCoordinates`, [`RouteReferenceChoice`], `TractionCurve`, `TrainColors`, [`TrainFilterButton`] (the train filter of the table windows), `TrainKeyList` and `DiagramKey` (the Trains and Key sections of a diagram window) and `SimulationHeaders` |
| `io/` | Interoperability formats. `LiveFrame` (the JSON frames of the live contract in `docs/live/frame-v1.schema.json`), [`LiveCommand`] (the requests of its clients and the checks on them) and [`LiveServer`] (the ZeroMQ transport) make the live stream. The vendored miniz is in `io/third_party/` |
| `util/` | Helpers used everywhere: `Log` (the logging core), `timeutil.hpp`, `TimeFormat`, `SpeedFormat`, `CsvWriter`, `TrajectoryUtil`, [`PlaybackProfiler`], `Version` and `portability.h` |
| `update/` | Release check, package download and self-update: [`UpdateChecker`], [`HttpTransport`], `WinHttpTransport`, `ReleaseInfo`, `Distribution`, [`SelfUpdater`], `UpdatePreparation`, `UpdateSettings`, [`UpdateRecord`], `UpdateNotices`, `UpdateTiming.h` and `WindowsStaging.h`. `UpdateHelper.cpp` is the source of the `egtrain_update_helper` program |
| `telemetry/` | Consent, queue and sender for usage and diagnostics events: [`TelemetryConsent`], [`TelemetryConsentDialog`], `TelemetryEvent`, `TelemetryOperation`, `TelemetryQueue` and `TelemetrySender` |
| `recovery/` | Recovery copies of a scene with unsaved edits. [`RecoveryStore`] keeps them below a folder that the caller names, [`RecoveryController`] writes them on a timer on one worker thread, and [`RecoveryPrompt`] offers the copies that an earlier run left behind. It has nothing to do with the recovery time of a timetable |
| `resources/` | Icons (`icons/`), the application icons for each system (`app/`) and the style sheet (`styles/egtrain.qss`) |
| `Scenes/` | The bundled cases, one folder each |
| `tests/` | One `test_<name>.cpp` for each test executable, with `fixtures/` (test data), `characterization/` (the characterization tests with their `expected/` files), `e2e/` (the runner of the scripted window scenarios), `live/` and the helper programs `update_probe*.cpp` and `live_client.cpp` |

## Completed-run data

When a run ends, `captureCompletedRun` (`diagrams/CompletedRunCapture.cpp`) copies what the result views need out of the
simulation into one `CompletedRunData`, and `RunController` holds it as `m_completedRun`, a
`std::shared_ptr<const CompletedRunData>`. The Run Results dock reads it, and every result window and CSV export keeps
its own pointer. So a window that was opened for one run goes on showing and exporting that run after another run of the
same case has replaced the simulation state. Opening or creating a case closes the result windows. The code in `diagrams/` reads the simulation only in `CompletedRunCapture.cpp` and
`RunResults.cpp`. `tools/e2e/test_diagrams_no_sim_globals.py` fails when another file of `diagrams/`, or
`app/MainWindowSupport.h`, includes a header of `simulation/` or names `regional_train`, `train_route`, `numRegions` or
`initial_variables`. It also fails when `app/MainWindow.cpp` or `app/MainWindow.h` names one of them: the window reads the
settings of the process through `RunController`.

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
| `egtrain_live` | `io/LiveCommand.cpp`, `io/LiveFrame.cpp`, `io/LiveServer.cpp` | nlohmann-json, `egtrain_util`, cppzmq |
| `egtrain_graphics` | `graphics/` | Qt Core, Gui and Widgets, `egtrain_scene`, `egtrain_util` |
| `egtrain_widgets` | `widgets/*.cpp` | Qt Core, Gui and Widgets, `egtrain_graphics`, `egtrain_util` |
| `egtrain_editors` | `editors/` | Qt Core, Gui and Widgets, `egtrain_scene`, `egtrain_diagrams`, `egtrain_widgets`, `egtrain_runselection`, `egtrain_util` |
| `egtrain_http` | `update/HttpTransport`, and on Windows `WinHttpTransport` | Qt Core and Network, and on Windows WinHTTP and Crypt32 |
| `egtrain_update` | `update/Distribution`, `ReleaseInfo`, `UpdateChecker`, `UpdatePreparation`, `UpdateRecord`, `UpdateSettings` | Qt Core and Network, `egtrain_http`, `egtrain_util` |
| `egtrain_runselection` | `app/RunSelection.cpp` | `egtrain_scene` |
| `egtrain_sceneopen` | `app/SceneOpenRequest.cpp` | Qt Core |
| `egtrain_openrelay` | `app/OpenRequestRelay.cpp` | Qt Core, Gui and Network |
| `egtrain_recovery` | `recovery/` | Qt Core, Gui and Widgets, `egtrain_scene` |
| `egtrain_telemetry` | `telemetry/` except `TelemetryConsentDialog` | Qt Core, Gui, Network and Widgets, `egtrain_http` |
| `egtrain_dispatch` | `app/DispatchController.cpp`, `app/GuiReplayHistory.cpp`, `simulation/RunControl.cpp`, `simulation/SimulationWorker.cpp` | Qt Core, Gui and Widgets, `egtrain_live`, `egtrain_sim` |
| `egtrain_diagrams` | `diagrams/` | Qt Core, Gui, Network, Widgets and Charts, `egtrain_sim` |

`egtrain_miniz` holds the vendored zip code. The first four libraries and
`scene_tool` use no Qt. The dependencies point one way; no library depends on `MainWindow`.
`egtrain_recovery` is linked by `QEGTRAIN`, `test_recoverystore`, `test_recoverycontroller` and `test_recoveryprompt`.

The update code and the telemetry sender make their HTTP requests through `HttpTransport`
(`update/HttpTransport.h`), a GET for the update code and a POST for the sender. The
function `createHttpTransport` selects the implementation that the application uses: Qt's network
manager, or on Windows `WinHttpTransport`, so the Windows package needs no TLS plugin of Qt. The WinHTTP
transport fetches https addresses only. A test passes `allowLoopbackHttp` to reach `http://127.0.0.1`
as well. `UpdateChecker` can take any transport, so `test_updatechecker` gives it a fake one, and
`test_httptransport` runs the transport of the application, for a GET and a POST, against a server on the loopback address.
`test_updatetransport_https` sends one real request to the GitHub release list through that transport
([HTTPS update check](../development/testing.md#https-update-check)).

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
[`RouteReferenceDialog`] also links one telemetry library.

Still compiled in more than one target: the telemetry sources of the hook flavours,
`TelemetryConsentDialog.cpp`, which the application builds without the consent hook
and `test_telemetryconsent` with it, `SelfUpdater.cpp`, which `test_selfupdater`
builds with `EGTRAIN_UPDATE_TEST_HOOK`, and `app/UpdateTelemetryController.cpp`, which
`test_updatetelemetrycontroller` builds again without the hooks. The update hook lets `test_selfupdater` replace the
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
| `myQGraphicsEllipseItem` | [`NodeItem`] |
| `myQGraphicsRectItem` | [`StationNodeItem`] |
| `myQGraphicsLineItem` | [`TrackLineItem`] |
| `myQGraphicsPixmapItem` | [`IconItem`] |
| `myQGraphicsColorizeEffect` | `HighlightEffect` |
| `connectionQGraphicsLineItem` | [`ConnectionItem`] |
| `passengerQGraphicsPixmapItem` | [`PassengerItem`] |
| `platformQGraphicsRectItem` | [`PlatformItem`] |
| `signallingQGraphicsEllipseItem` | [`SignalItem`] |
| `trainQGraphicsItemGroup` | [`TrainItemGroup`] |
| `trainQGraphicsPolygonItem` | [`TrainBodyItem`] |
| `virtualArcQGraphicsLineItem` | [`VirtualArcItem`] |
| `timeQProgressBar` | `TimeProgressBar` |
| `infoQDockWidget` | `InfoDockWidget` |
| `EGTRAIN` (class) | `DispatchController` |
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

[`MainWindow`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_main_window.html
[`DispatchController`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_dispatch_controller.html
[`GuiSimulationSnapshot`]: https://ancientkingg.github.io/EGTRAIN/latest/api/struct_gui_simulation_snapshot.html
[`GuiReplayHistory`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_gui_replay_history.html
[`ReplayCoverage`]: https://ancientkingg.github.io/EGTRAIN/latest/api/struct_replay_coverage.html
[`FollowAvailability`]: https://ancientkingg.github.io/EGTRAIN/latest/api/struct_follow_availability.html
[`RunSelection`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_run_selection.html
[`OpenRequestRelay`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_open_request_relay.html
[`SceneSession`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_scene_session.html
[`UpdateTelemetryController`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_update_telemetry_controller.html
[`RunController`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_run_controller.html
[`ResultViews`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_result_views.html
[`RunResultsDock`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_run_results_dock.html
[`SimulationRun`]: https://ancientkingg.github.io/EGTRAIN/latest/api/struct_simulation_run.html
[`SimulationWorker`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_simulation_worker.html
[`RunControl`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_run_control.html
[`NumberGenerator`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_number_generator.html
[`InitialParameters`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_initial_parameters.html
[`SceneModel`]: https://ancientkingg.github.io/EGTRAIN/latest/api/struct_scene_model.html
[`SceneDiagnostic`]: https://ancientkingg.github.io/EGTRAIN/latest/api/struct_scene_diagnostic.html
[`SceneFieldReader`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_scene_field_reader.html
[`SceneHistory`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_scene_history.html
[`StagedDirectory`]: https://ancientkingg.github.io/EGTRAIN/latest/api/struct_staged_directory.html
[`SceneDocument`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_scene_document.html
[`SceneEditorPanel`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_scene_editor_panel.html
[`CaseSettingsPanel`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_case_settings_panel.html
[`CompositionPanel`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_composition_panel.html
[`TrainUnitPanel`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_train_unit_panel.html
[`ServicePanel`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_service_panel.html
[`TimetableStopsPanel`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_timetable_stops_panel.html
[`PassengerPanel`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_passenger_panel.html
[`ScenarioPanel`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_scenario_panel.html
[`IncidentPanel`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_incident_panel.html
[`EntranceDelayPanel`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_entrance_delay_panel.html
[`InfrastructurePanel`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_infrastructure_panel.html
[`ValidationPanel`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_validation_panel.html
[`TrainUnitSourceLinker`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_train_unit_source_linker.html
[`InfrastructureFacet`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_infrastructure_facet.html
[`NetworkPresenter`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_network_presenter.html
[`RuntimeGeometrySource`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_runtime_geometry_source.html
[`NetworkScene`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_network_scene.html
[`NetworkView`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_network_view.html
[`FollowCamera`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_follow_camera.html
[`SignalGeometry`]: https://ancientkingg.github.io/EGTRAIN/latest/api/struct_signal_geometry.html
[`SelectionCueItem`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_selection_cue_item.html
[`HighlightEffect`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_highlight_effect.html
[`StationOverlayItem`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_station_overlay_item.html
[`TrainBadgeItem`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_train_badge_item.html
[`ConsoleWidget`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_console_widget.html
[`InfoDockWidget`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_info_dock_widget.html
[`InspectorPanel`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_inspector_panel.html
[`TimeProgressBar`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_time_progress_bar.html
[`AboutDialog`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_about_dialog.html
[`CaseChooserDialog`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_case_chooser_dialog.html
[`NetworkLegendWidget`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_network_legend_widget.html
[`ColorChoiceButton`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_color_choice_button.html
[`ChoiceComboBox`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_choice_combo_box.html
[`CompactDoubleSpinBox`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_compact_double_spin_box.html
[`ElidedLabel`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_elided_label.html
[`UserPrompts`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_user_prompts.html
[`CompletedRunData`]: https://ancientkingg.github.io/EGTRAIN/latest/api/struct_completed_run_data.html
[`RunResults`]: https://ancientkingg.github.io/EGTRAIN/latest/api/struct_run_results.html
[`DiagramWindow`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_diagram_window.html
[`TimetableTableWindow`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_timetable_table_window.html
[`DelayAnalysisWindow`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_delay_analysis_window.html
[`AreaCapacityWindow`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_area_capacity_window.html
[`BlockingTimeScope`]: https://ancientkingg.github.io/EGTRAIN/latest/api/struct_blocking_time_scope.html
[`RouteReferenceChoice`]: https://ancientkingg.github.io/EGTRAIN/latest/api/struct_route_reference_choice.html
[`TrainFilterButton`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_train_filter_button.html
[`LiveCommand`]: https://ancientkingg.github.io/EGTRAIN/latest/api/struct_live_command.html
[`LiveServer`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_live_server.html
[`PlaybackProfiler`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_playback_profiler.html
[`UpdateChecker`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_update_checker.html
[`HttpTransport`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_http_transport.html
[`SelfUpdater`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_self_updater.html
[`UpdateRecord`]: https://ancientkingg.github.io/EGTRAIN/latest/api/struct_update_record.html
[`TelemetryConsent`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_telemetry_consent.html
[`TelemetryConsentDialog`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_telemetry_consent_dialog.html
[`RecoveryStore`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_recovery_store.html
[`RecoveryController`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_recovery_controller.html
[`RecoveryPrompt`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_recovery_prompt.html
[`RouteReferenceDialog`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_route_reference_dialog.html
[`NodeItem`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_node_item.html
[`StationNodeItem`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_station_node_item.html
[`TrackLineItem`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_track_line_item.html
[`IconItem`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_icon_item.html
[`ConnectionItem`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_connection_item.html
[`PassengerItem`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_passenger_item.html
[`PlatformItem`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_platform_item.html
[`SignalItem`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_signal_item.html
[`TrainItemGroup`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_train_item_group.html
[`TrainBodyItem`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_train_body_item.html
[`VirtualArcItem`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_virtual_arc_item.html
