# Code documentation investigation

This page records what the source under `EGTRAIN/QEGTRAIN/` says about itself before documentation comments are written. It covers the comment counts per folder, the legacy simulation core, conventions the code uses without stating them, comments that are dead or misleading, questions the code does not answer, and the order in which the folders should be documented.

Counts and line numbers are for `main` at commit `f299a252` (19 October 2026). Paths are relative to `EGTRAIN/QEGTRAIN/` unless they start with `docs/`. Line numbers move with every edit, so check a reference against the current file before relying on it.

## How the numbers were made

- Folders: `app/`, `diagrams/`, `editors/`, `graphics/`, `io/`, `recovery/`, `scene/`, `simulation/`, `telemetry/`, `update/`, `util/` and `widgets/`, with their subfolders. Excluded: `tests/`, `io/third_party/`, and generated Qt files (none are tracked).
- Lines: every line of every `.cpp`, `.h` and `.hpp` file, as `wc -l` counts them.
- Documentation comment lines: lines with `///`, `//!` or `/**` followed by anything but `*`. A banner such as `/*****` is not counted. Doxygen was not run, so its count of undocumented members is not available yet.
- Ordinary comments: lines that hold only a `//` or `/* */` comment, as a share of all lines. Comments after code are counted apart.
- Classes: `class` and `struct` definitions in headers, nested ones included, enums excluded. Public functions: function declarations and inline definitions in headers that are free or in a public section. The count of public functions is approximate.

## Coverage per folder

| Folder | Files | Lines | Classes | Public functions | Doc comment lines | Comment-only lines | Share | Comments after code |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `app/` | 40 | 12,420 | 36 | 230 | 0 | 953 | 7.7 % | 78 |
| `diagrams/` | 37 | 8,424 | 68 | 113 | 0 | 462 | 5.5 % | 83 |
| `editors/` | 40 | 10,260 | 20 | 87 | 0 | 537 | 5.2 % | 42 |
| `graphics/` | 44 | 6,745 | 34 | 268 | 0 | 547 | 8.1 % | 86 |
| `io/` | 10 | 1,225 | 4 | 24 | 0 | 144 | 11.8 % | 25 |
| `recovery/` | 6 | 1,481 | 10 | 18 | 0 | 159 | 10.7 % | 34 |
| `scene/` | 39 | 13,638 | 80 | 174 | 0 | 377 | 2.8 % | 74 |
| `simulation/` | 31 | 12,872 | 36 | 251 | 13 | 1,254 | 9.7 % | 742 |
| `telemetry/` | 12 | 2,231 | 22 | 71 | 0 | 65 | 2.9 % | 16 |
| `update/` | 22 | 3,282 | 17 | 52 | 0 | 264 | 8.0 % | 11 |
| `util/` | 18 | 1,370 | 10 | 66 | 4 | 103 | 7.5 % | 19 |
| `widgets/` | 25 | 3,364 | 23 | 109 | 0 | 306 | 9.1 % | 34 |
| Total | 324 | 77,312 | 360 | about 1,460 | 17 | 5,171 | 6.7 % | 1,244 |

What the counts show:

- Seventeen documentation comment lines exist. Thirteen are `//!` and `/**` lines in `simulation/RollingStock.h` (lines 300, 301, 304, 307, 310, 313, 318, 346 to 348, 494, 503 and 551) and four are XML-style `///` lines inside the function body of `simulationTime` in `util/timeutil.hpp` (lines 9 to 12). The `/***` banner at `simulation/RollingStock.h:143` makes the first count 14 if banners are not excluded.
- One more Doxygen-style member comment exists: `/*!< number of Wagons*/` at `simulation/RollingStock.h:172`.
- `simulation/` has 742 comments after code: 209 in `Signalling.cpp`, 128 in `RollingStockBlockingTimes.cpp`, 92 in `RollingStock.h` (the members of `Train`) and 58 in `Infrastructure.h`. They carry most of the units and meanings, but Doxygen reads a comment after a member only in the forms `///<`, `//!<`, `/**<` or `/*!<`.
- `scene/` and `telemetry/` have the lowest comment share (2.8 % and 2.9 %). `scene/` has the most classes (80).
- `simulation/RollingStock.h` has 620 lines, `simulation/Signalling.cpp` 3,431 and `app/MainWindow.cpp` 3,946. Most of the logic with the least explanation sits in the first two.

## What each folder does and who calls it

"Used by" is the set of other folders, tests excluded, that include a header of the folder, found by searching for `#include "<folder>/` in the source. The numbers are counts of include lines. The descriptions come from [Source layout](../architecture/source-layout.md), which lists every file.

| Folder | What it does | Main classes | Used by |
| --- | --- | --- | --- |
| `app/` | Program entry, main window, and the controllers that open, run and save a scene | `MainWindow`, `DispatchController`, `RunController`, `SceneSession`, `ResultViews` | `editors/` (5), `diagrams/` (4), `graphics/`, `io/`, `simulation/` and `widgets/` (1 each); see below |
| `diagrams/` | Copies a finished run into `CompletedRunData`, builds the Run Results table, the CSV exports and the chart windows | `CompletedRunCapture`, `RunResults`, `RunExports`, `DiagramWindow`, `DelayAnalysis` | `app/` (24), `editors/` (1), `simulation/Simulation.cpp:2` (for `energyMJKWh`) |
| `editors/` | The scene editor panels and the open scene with its unsaved state | `SceneDocument`, `SceneEditorPanel`, the `*Panel` classes, `InfrastructureFacet` | `app/` only (20) |
| `graphics/` | The network canvas: scene, view, items, follow camera | `NetworkPresenter`, `NetworkScene`, `NetworkView`, `RuntimeGeometrySource` | `app/` (28), `widgets/` (15) |
| `io/` | Live stream (JSON frames, commands, ZeroMQ server) | `LiveFrame`, `LiveCommand`, `LiveServer` | `app/` (4). `scene/SceneBundle.cpp:3` includes the vendored `miniz` from `io/third_party/` |
| `recovery/` | Recovery copies of a scene with unsaved edits | `RecoveryStore`, `RecoveryController`, `RecoveryPrompt` | `app/` (3) |
| `scene/` | The canonical scene model, its loading, validation, saving, bundling, import and export | `SceneModel`, `SceneValidator`, `SceneWriter`, `SceneBundle`, `SceneImporter`, `SceneExporter` | `editors/` (41), `app/` (32), `simulation/` (17), `diagrams/`, `graphics/`, `recovery/`, `widgets/` |
| `simulation/` | The simulation engine, the builders that fill a run from a scene, and the run state | `SimulationRun`, `Train`, `Regional`, `Section`, `Route`, `Passenger` | `app/` (`DispatchController`, `RunController`, `main`, `MainWindow`, `RuntimeGeometryFromRun`), `diagrams/` (`CompletedRunCapture`, `RunResults.h`, `SimulationHeaders.h`), `graphics/` (items and `NetworkPresenter`) |
| `telemetry/` | Consent, queue and sender for usage and diagnostics events | `TelemetryConsent`, `TelemetryQueue`, `TelemetrySender` | `app/` (7), `diagrams/` (4) |
| `update/` | Release check, package download and self-update | `UpdateChecker`, `SelfUpdater`, `HttpTransport`, `UpdatePreparation` | `app/` (7), `telemetry/` (1) |
| `util/` | Logging, time and speed formatting, CSV, trajectory helpers, playback profiler | `Log`, `PlaybackProfiler`, `TimeFormat`, `CsvWriter` | Every other folder except `scene/`, `recovery/` and `telemetry/` |
| `widgets/` | Dock widgets, dialogs and small controls | `InspectorPanel`, `ConsoleWidget`, `CaseChooserDialog`, `NetworkLegendWidget` | `editors/` (36), `app/` (25), `diagrams/` (12), `recovery/`, `telemetry/` |

Some includes run from a lower folder into `app/` or `diagrams/`, and they will show in the generated include graphs:

- `app/DialogFolders.h` from four windows in `diagrams/` and from `editors/ScenarioPanel.cpp:3`;
- `app/RunSelection.h` from four headers in `editors/` (it is its own library, `egtrain_runselection`);
- `app/GuiSimulationSnapshot.h` from `io/LiveFrame.h:4`, `graphics/NetworkPresenter.cpp:15` and `widgets/InspectorPanel.cpp:13`;
- `app/DispatchController.h` from `simulation/SimulationWorker.cpp:2`, which is built in the same library;
- `diagrams/RunResults.h` from `simulation/Simulation.cpp:2`.

`DialogFolders.h` and `GuiSimulationSnapshot.h` have no source file, and `energyMJKWh` is an inline function, so the libraries still link one way as [Source layout](../architecture/source-layout.md#libraries) says.

## The legacy simulation core

The folder is `simulation/`. Existing pages that explain a part: [Signalling levels](../architecture/signalling-levels.md) (the step, the codes, every level), [Scene model](../architecture/scene-model.md) and [Scene schema](../architecture/scene-schema.md) (the input, units of the input keys, dwell rule), [External state sharing](../architecture/external-sharing.md) (the live stream) and [Run output](../guides/run-output.md) (the files). No page describes the data structures or the equations of motion.

### Files

| Files | Lines | Role |
| --- | --- | --- |
| `SimulationRun.h`, `.cpp` | 161, 82 | The state of one run: trains, infrastructure, passengers, statistics |
| `Infrastructure.h`, `.cpp` | 289, 180 | `Node`, `Arc`, `BlockSet`, `Connections`, `Stations`, `StationPlatform`, the constant `timestep` |
| `Signalling.h`, `.cpp` | 456, 3,431 | `Section`, `Route`, `TDS`, `MovementAuthority`, event records, and the routines that set codes and authorities |
| `RollingStock.h` | 620 | `Train` and `Regional`, `BlockingTimes` |
| `RollingStock.cpp`, `RollingStockDynamics.cpp`, `RollingStockTrajectory.cpp`, `RollingStockBraking.cpp`, `RollingStockOccupation.cpp`, `RollingStockStops.cpp`, `RollingStockBlockingTimes.cpp` | 4,032 together | The members of `Train`: setup and outputs, forces and energy, the step, braking, occupied sections, timetable points, blocking times after the run |
| `Simulation.h`, `.cpp` | 63, 560 | Delay statistics, energy outputs, the passenger and train interaction of a step |
| `Passengers.h`, `.cpp` | 126, 157 | `Trip`, `Journey`, `Passenger`, lookup indexes |
| `InitialParameters.h`, `.cpp`, `NumberGenerator.h`, `.cpp`, `Optimisation.h`, `.cpp` | 48, 21, 42, 63, 9, 76 | Process options, random numbers, departure time spacing |
| `RunControl.h`, `.cpp`, `SimulationWorker.h`, `.cpp` | 79, 79, 54, 44 | Pause, stop and delay of a run on its own thread |
| `builders/` | 1,068 and 1,121 | `InfrastructureBuilder` and `OperationsBuilder` fill a run from a `SceneModel` |

### Main data structures

- `SimulationRun` (`simulation/SimulationRun.h:77`) holds everything one run builds and the next must not see: `trains`, `platforms`, `passengers`, the delay statistics, `etcsMa` (movement authorities of the step), `blocksOccupied` and `blocksConnected` (lists that a step fills and clears), `incidents`, and `infra`. Its comment (lines 66 to 76) says that a new member must be cleared in `reset()` and, if it points at another member, in `adopt()`.
- `RunInfrastructure` (`simulation/SimulationRun.h:24`) holds `blockSets` (tracks), `connections`, `stations`, `sections` (block sections), `routes`, the detection sections and the single-track data. The arrays of tracks, connections and stations have the fixed sizes `kMaxTrackLines` 268, `kMaxConnections` 708 and `kMaxStations` 95 (`simulation/Infrastructure.h:208` to `210`).
- `Node`, `Arc`, `BlockSet` (`simulation/Infrastructure.h:71`, `151`, `190`): a track is a chain of nodes joined by arcs. An arc carries `length`, `curvature`, `gradient`, `speedLimit` and `signalSpeedLimit`.
- `Section` (`simulation/Signalling.h:134`) is a block section: up to 20 arcs, a `code`, a `state` (`SignalState`), a `SignallingLevel`, and the connected sections. `Route` (`simulation/Signalling.h:280`) is a sequence of copies of sections, plus `reversed_direction` for a route driven against its definition.
- `Train` (`simulation/RollingStock.h:168`) holds the physical data, one vector per sampled quantity indexed by step (`instant_train_speed`, `instant_spatial_position`, `instant_train_power_consumption`, `instant_train_energy_consumption`, `Eq`, `BX`, `Xob`, `Vob`), the stop and station arrays (capacity 40, `RuntimeLimits::kMaxTimetableStops`), the braking plan (`Sbrak`, `Vbrak`, 2,000 entries each, line 251), and the flags of movement authority and virtual coupling. `Regional` adds no member (`static_assert` at line 600). A train reaches its run through `run_`.
- `MovementAuthority` (`simulation/Signalling.h:317`) is an end of authority for levels 3 and 4 and for a failed signal at every level. `TrainEvent` and `InfraEvent` (`simulation/Signalling.h:46`, `80`) record passages.
- `BlockingTimes` and `BlockingTimeList` (`simulation/RollingStock.h:64`, `114`) hold the post-run blocking times of one train.
- `Passenger`, `Journey`, `Trip` (`simulation/Passengers.h:57`, `32`, `12`): a passenger has journeys, a journey has trips (legs). `PassengerIndex` and `PlatformIndex` look entries up by ID.

### From scene to run

`DispatchController::prepareScene` (`app/DispatchController.cpp:223`) validates the scene and builds a run apart from the live one with `buildInfrastructureAndSignallingFromScene` and `buildOperationsFromScene` (lines 280 and 294; declared at `simulation/Signalling.h:19` and `simulation/RollingStock.h:45`), then swaps it in with `SimulationRun::adopt` (line 336; `simulation/SimulationRun.h:136` to `140`). [Getting started](getting-started.md#from-open-to-saved) step 4 shows the path from the window. The vectors of a train get one slot per step, `ceil(duration / timestep)` (`simulation/builders/OperationsBuilder.cpp:979`).

### The time step loop

`DispatchController::Train_Simulation_Mixed_Signalling_With_Passengers` (`app/DispatchController.cpp:663`) runs `for t = 0 while t < settings.times` (line 682). `timestep` is 1 second (`simulation/Infrastructure.h:25`). One step does this, in this order:

1. `RunControl::beginStep` applies queued pause, stop and delay commands (line 684).
2. Passengers whose journey starts at `t + startingSimulationTime` enter the network (`checkJourneyStartForAllPassengers`, line 698). With the passenger view on, every platform's waiting list is refreshed (line 704).
3. Every train moves, in the order of the run except that trains that are due and still waiting take the places of the others in order of due time (lines 715 to 725). Each calls `trajectoryComputationIncludingMovingBlock(t, ...)`, then records its station passages and its arrival or departure at the ends of its route (lines 726 to 733).
4. For every train, `Simulate_Train_Passenger_Interactions` lets passengers alight and board and may lengthen the stop (line 744).
5. The signalling for the next step: `etcsMa` is cleared, then `Occupy_Block_Sections_Of_Route`, `Apply_Signal_Failures_Mixed_Signalling`, `Apply_Single_Track_Authorities_Mixed_Signalling`, `ReportAllTrainPositionsToRBC` (margin 50 m), `protectStationAreas`, `releaseMixedSignallingSystem`, `activateMixedSignallingSystem` and `unlockDoubleSwitches` run, and the occupied and connected lists are cleared (lines 792 to 816). [Signalling levels](../architecture/signalling-levels.md#what-happens-in-a-step) explains each.
6. `publishSimulationSnapshot(t)` hands the step to the window (line 823).

After the loop the passenger status file is written (line 832). `runSimulation` (`app/DispatchController.cpp:532`) sets every `departure_time` to `scheduled_departure_time` before the loop (line 571) and then runs the stages after the steps: energy totals (580), timetable points (604), delay statistics (607 to 622), blocking times (632), and the optional per-train files (645 to 652). Between the stages it checks for a stop request.

### What one train does in a step

`Train::trajectoryComputationIncludingMovingBlock` (`simulation/RollingStockTrajectory.cpp:321`) reads the previous step (`t - 1`) and writes step `t`:

1. At `t = 0` it only sets the start state (line 323). A train under a breakdown incident holds its state and sets `Eq = 5` (336 to 346).
2. `checkEntrance` (line 38) lets the train enter when `t >= departure_time` and the first section allows it: code 270 or 180 at the fixed-block levels, no movement authority of another train at the start at levels 3 and 4. A waiting train is also held back by a failed first section and, at a signalling level, by a first section that another train entered in this step or by a crossover conflict (lines 43 to 50).
3. It finds its section and arc from the position, and takes the speed limit as the lowest of its own maximum, the arc limit and the signal limit, then applies an incident cap (379 to 388).
4. It computes the candidate acceleration step (lines 434 to 465) and asks `europeanVitalComputerWithListsImproved` (`simulation/RollingStockBraking.cpp:491`) for the next braking point (line 467).
5. It accelerates (line 474), brakes (499) or cruises (548). Braking uses a stored curve (`Sbrak`, `Vbrak`) or `fullBrakingStep`.
6. For a train in virtual coupling it checks coupling and following, and may replace the step by one that follows the leader (lines 566 to 669).
7. If the train passed the end of its route it records `End_Time`, sets its position to -9999 and leaves the simulation (673 to 687).
8. If it stands within 4 m of the end of an arc it holds there for a station dwell, a checkpoint, a red state or an authority (692 to 828), and then updates the infrastructure elements it passed (830 to 838).

### Signalling levels

The model has levels 0 to 5 and "no level" (`scene/SignallingLevel.h:5` to `16`). Levels 0, 1, 2 and 5 are fixed block: after each step the sections behind an occupied one get a code (270, 180, 75, 751 or 0), a state and a signal speed limit (`simulation/Signalling.cpp:2744` to `2835`). Levels 3 and 4 are moving block: positions go to a radio block centre (`ReportPositionToRBC`, `simulation/RollingStockOccupation.cpp:93`) and become movement authorities that the train brakes for. Level 4 adds following a leader. The codes, each level, mixed routes, areas and known limits are in [Signalling levels](../architecture/signalling-levels.md); none of it is repeated here.

### Braking and traction

- Tractive effort is a quadratic of speed per speed band, `c0 + c1 v + c2 v^2`, scaled by the service performance percentage (`simulation/RollingStockDynamics.cpp:6` to `16`). The band limits are the first two numbers of each traction curve row ([Scene schema](../architecture/scene-schema.md#rolling_stockjson)).
- Resistance is the sum of a traction unit term, a wagon term (Sauthoff, `simulation/RollingStock.h:304`), a gradient term `g m G` and a curvature term `g m 700 / (R 1000)` (`simulation/RollingStockDynamics.cpp:28` to `58`).
- Acceleration uses `v' = v + (F - R) / (m k) dt`, where `k` is `massFactor`, capped at the speed limit (`simulation/RollingStockTrajectory.cpp:439` to `448`).
- Braking effort ramps with the jerk: `m k min(jerk t, a_max)` (`simulation/RollingStockDynamics.cpp:18` to `26`). A braking step with full force decelerates by `a_max + R / (m k)` (`simulation/RollingStockBraking.cpp:855` to `863`).
- `europeanVitalComputerWithListsImproved` collects the nodes of the arcs ahead in the train's own section and the eight sections after it (`simulation/RollingStockBraking.cpp:530`), adds the closest end of authority of levels 3 and 4 and of a failed signal, and returns the braking point of the target that needs the earliest braking. It keeps a stored plan (`Sbrak`, `Vbrak`) while the plan stays valid (lines 491 to 752). `simulation/RollingStock.h:441` describes it as the reaction of the driver to the most severe service limit.
- Energy: the power sample is force times the speed of the previous step (`simulation/RollingStockTrajectory.cpp:481`); energy adds the mean of two samples times `timestep`, divided by 10^6, so the vector is in MJ (lines 15 to 28). Negative power counts as zero. Totals add 10 % for auxiliaries and divide by the substation efficiency 0.8; regenerated power is scaled by 0.7 (`simulation/RollingStockDynamics.cpp:80` to `117`, `simulation/Simulation.cpp:188`).

### Dwell and passengers

- A train that stands at a station arc counts from `stop`. It leaves when `t - stop > StopTime`, `t > ScheduledDepartures[s]` and no authority holds it, and at a stop with an order to respect also when the aspect is not restrictive, and at a platform at the end of a block also when the signal is not red (`simulation/RollingStockTrajectory.cpp:698` to `762`). The dwell rule is in [Scene schema](../architecture/scene-schema.md#servicesjson).
- Passengers alight if they are on this train and their arrival station is the stop, then board in the order of the platform's waiting list, which is sorted by the actual departure time of the trip each passenger waits for (`simulation/Simulation.h:47` to `49`), while the train has room. Capacity is 300 for the unit and 300 for each wagon (`simulation/RollingStock.cpp:100` to `103`). A transfer to a platform two or more numbers away adds 30 seconds per number of difference (`simulation/Simulation.cpp:449` to `457`).
- When a stop has its first step, the passenger-dependent dwell replaces `StopTime` if it is longer (`simulation/Simulation.cpp:543` to `558`). The model is the one named in the comment at `simulation/RollingStock.cpp:112` (Fernandez et al. 2007, extending Gibson et al. 1989), with eight parameters set in `simulation/builders/OperationsBuilder.cpp:253`. Congestion flags switch on at platform occupancy above 0.65 and train occupancy above 0.7 (`simulation/RollingStock.cpp:121`, `127`).
- A passenger who cannot board only produces a log line (`simulation/Simulation.cpp:531` to `535`).

### Global state the code depends on

The routines take the run as a parameter, and `Train::run_` points at it ([Source layout](../architecture/source-layout.md#completed-run-data)). Process-wide state remains in these places:

| State | Defined | Used for |
| --- | --- | --- |
| `initial_variables` (`InitialParameters`) | `simulation/Infrastructure.cpp:9`, declared `extern` at `simulation/RollingStock.h:38` | Options of the process: case name, duration override, seed, output folder, live stream settings. `simulation/builders/OperationsBuilder.cpp:283`, `328`, `963` and `1105` read and write it. `prepareScene` copies it into `run.settings` (`app/DispatchController.cpp:335`) |
| `simulation` (`DispatchController`) | `app/DispatchController.cpp:212`, declared at `app/DispatchController.h:119` | The controller that `SimulationWorker(QObject*)` uses (`simulation/SimulationWorker.cpp:9`) |
| `defaultRun()` | `simulation/SimulationRun.cpp:80` | The run of the process; its address never changes. A test may pass its own run |
| `SimulationWorker::s_active` | `simulation/SimulationWorker.cpp:6` | Lets `runSimulation` find its worker's `RunControl` (`app/DispatchController.cpp:547`) |
| `timestep`, `signalCode1` to `3`, `kMax*` | `simulation/Infrastructure.h:25`, `27`, `208` to `210` | Constants: 1 s, 11.111 m/s, 0 and 0, and the array sizes |
| Logging state | `util/Log.cpp:25`, `35` | Category mask and sinks |
| `PlaybackProfiler::instance()` | `util/PlaybackProfiler.h`, called at `app/DispatchController.cpp:508` and `529` | Playback timing |
| A static counter | `app/DispatchController.cpp:230` | Counts calls of `prepareScene` for the startup timing detail, which two environment variables switch on |

## Names, units and conventions the code uses without saying so

| Convention | Source |
| --- | --- |
| A step index `t` is seconds from the start of the run. Vectors of a train are indexed by it and `timestep` is 1 s. `departure_time`, `StationArrivals` and `ScheduledDepartures` are in the same seconds. They are relative to the base time of the scene ([Scene schema](../architecture/scene-schema.md#servicesjson)) | `simulation/Infrastructure.h:24` to `25`, `simulation/builders/OperationsBuilder.cpp:979` |
| Passenger times are seconds since midnight: the step plus `startingSimulationTime` | `simulation/Simulation.cpp:277`, `docs/architecture/scene-schema.md` (journey windows) |
| `Node::X` and `Node::Y` are kilometres, and a position in metres is `X * 1000` | `simulation/builders/InfrastructureBuilder.cpp:679` (`xKm`), `simulation/RollingStockTrajectory.cpp:326` (`Start_Node_X * 1000`), `simulation/Infrastructure.cpp:77` |
| `instant_spatial_position[t]` is the head position in metres along the route, and -9999 after the train has left | `simulation/RollingStockTrajectory.cpp:679` |
| `Train::trainXPosition` returns kilometres and -1 when the head is on no section | `simulation/RollingStock.cpp:621` to `649`, `docs/architecture/external-sharing.md` |
| `Arc::length` is metres, `Section::length` is a difference of kilometre coordinates | `simulation/Infrastructure.cpp:77`, `simulation/builders/InfrastructureBuilder.cpp:812` |
| `Arc::gradient` is rise per length (0.01 is a 1 % slope), although the scene key and field are called `gradient_percent` and `gradientPercent`. The schema says this, and that a route driven in reverse sees the opposite sign; the code does not | `scene/SceneModel.h:47`, `simulation/builders/InfrastructureBuilder.cpp:692`, `simulation/RollingStockDynamics.cpp:42`, `docs/architecture/scene-schema.md` (arcs) |
| `Arc::curvature` holds the radius of curvature in metres (`curvature_radius_m`), and 0 means straight | `simulation/builders/InfrastructureBuilder.cpp:691`, `simulation/RollingStockDynamics.cpp:46` to `51` |
| Speeds are m/s inside the simulation and km/h at the edges (`configuredMaximumSpeedKmh`, `speedKmhAt`). Forces are N, power W, energy vectors MJ, text outputs kWh with the factor 0.27778 | `simulation/RollingStock.h:183` to `185`, `555`, `diagrams/RunResults.h:146` to `147` |
| Mass is kg. `massFactor` multiplies the mass in the equation of motion. `massPerWagonAxle` holds the total mass of the wagons, not a mass per axle | `simulation/builders/OperationsBuilder.cpp:237`, `simulation/RollingStockTrajectory.cpp:442` |
| `Section::code` 270, 180, 75, 751 and 0 are aspect codes: 270 green, 180 yellow, 75 red, 751 red-red, and 0 on an occupied or failed section | [Signalling levels](../architecture/signalling-levels.md#codes-and-what-they-do-to-a-following-train) |
| A signalling level is 0 to 5, and -99999999 means none | `scene/SignallingLevel.h:16` |
| `Route::reversed_direction` is true when the train runs against the direction in which the route is defined. Positions on a reversed route are measured from the other end, and outputs subtract them from `OriginalRefReversedRoute` | `simulation/Signalling.h:287` to `289`, `simulation/RollingStockTrajectory.cpp:141` to `148` |
| A stopped train is parked 0.0001 m short of the stop point and released at 0.0001 m/s and 0.0001 m past it. A train within 4 m of the end of an arc counts as standing at it | `simulation/RollingStock.h:153` to `166`, `simulation/RollingStockTrajectory.cpp:699`, `730` to `731` |
| `Train::Eq[t]` is a label of the branch that produced step `t`: 1 accelerate, 12 cruise, 5 breakdown hold or station dwell, 6 at a signal, 52, 53 and 55 braking from a stored or a new plan, 54 full braking, 323 hold at an authority, 532 and 534 checkpoint release and hold, 958 zero speed limit, 1002 following a leader. Only 54 is read outside tests | `simulation/RollingStockTrajectory.cpp:344`, `479` to `820`, `simulation/RollingStockBraking.cpp:827` to `862`, `simulation/RollingStockTrajectory.cpp:520` |
| `Train::temp` is the last step before braking began and `Train::stop` the last step in which the train moved, which starts the dwell count | `simulation/RollingStockTrajectory.cpp:483` to `484`, `535`, `716` to `717` |
| `MovementAuthority::type` and `typePart` are strings ("SignalFailure", "TrainEnd", "TrainUncoupled"; "Front", "Tale") and a passenger's `CurrentStatus` is one of "None", "OnPlatform", "OnBoard". "None" is the empty value of many ID strings, and -9999 or -10000 of many numbers | `simulation/RollingStockTrajectory.cpp:84`, `simulation/Passengers.h:24`, `26`, `simulation/Signalling.cpp:78` |
| Fixed capacities: 20 arcs per section, 10 connected sections, 40 ETCS 3 braking points, 20 speed bands, 40 timetable stops | `simulation/Signalling.h:140`, `154`, `161`, `simulation/RollingStock.h:178`, `scene/RuntimeLimits.h:10` |
| Constants of the model: 50 m margin of levels 3 and 4, 30 m and 0.278 m/s of coupling, eight sections of look-ahead, the blocking time arguments 5, 3 plus the buffer time, 0.5 and 50 for setup, release, sight reaction time and safety margin, with no unit stated at the call | `app/DispatchController.cpp:632`, `804`, `simulation/RollingStockTrajectory.cpp:574` to `575`, `623`, `simulation/RollingStockBraking.cpp:530` |

## Dead or misleading comments

| Where | Problem |
| --- | --- |
| `simulation/RollingStock.h:329` to `344` | A commented-out earlier `checkEntrance` with IDs such as 111 and 113. The current `checkEntrance` is at `simulation/RollingStockTrajectory.cpp:38` and works differently |
| `simulation/RollingStock.h:346` to `348` | The `//!` comment "Check the entrance possibility" is above `entryConflictsWithCrossover`, not `checkEntrance` (line 351) |
| `simulation/RollingStock.h:258` | Says `Eq` is the "Number of speed interval of the Traction curve function". It holds the branch label of each step (see the conventions above) |
| `simulation/RollingStock.h:44` | Says errors leave "operation globals" intact. The function writes the run and, at its end, one global, `initial_variables` (`simulation/builders/OperationsBuilder.cpp:1105`) |
| `simulation/Signalling.h:17` | "Build the infrastructure/signalling globals": the function writes `run.infra` |
| `simulation/Infrastructure.h:156` | Says `speedInBraking` is the mean speed in the braking zone. The code sets it to the speed to reach at the end of the arc: the limit of the next arc (`simulation/Signalling.cpp:2702` to `2716`), the lower of the next section's limits or 0 before an occupied section (`2839` to `2895`), and 0 before a station (`simulation/RollingStockTrajectory.cpp:398`) |
| `simulation/Infrastructure.h:156` | `brakingDistance` is set to 0 (`simulation/Infrastructure.cpp:73`) and only copied and compared afterwards; nothing gives it another value |
| `simulation/Signalling.h:321` | Says `typePart` is "Front or Tail". The code writes and compares "Tale" (`simulation/RollingStockTrajectory.cpp:84`, `304`, `simulation/RollingStockBraking.cpp:580`) |
| `simulation/Signalling.h:247` | `friend void SetBlockSpeed();` names a function that does not exist |
| `simulation/Signalling.h:244`, `266`, `simulation/Infrastructure.h:243` | Parameters named `blockSets` are a section (to reverse, to compare) and a pair of passenger and time. `areBlocksConnected` tests overlap (`simulation/Signalling.cpp:808` to `811`), not connection |
| `simulation/Passengers.h:65`, `68` | Name the status "Onboard". The code uses "OnBoard" (`simulation/Simulation.cpp:354`, `505`) |
| `simulation/Passengers.h:70` | Says `IsIntheNetwork` is true when a passenger enters and also when it has exited. The code sets it false on exit (`simulation/Simulation.cpp:390`) |
| `simulation/RollingStock.h:236`, `243` | `IsTrainCoupling` is described as an int that "counts the number of times" but is a `bool` that is set and cleared (`simulation/RollingStockBraking.cpp:493`, `747`, `simulation/RollingStockTrajectory.cpp:578`). `delayed` is an `int` described as a boolean |
| `simulation/RollingStock.h:191` | "the departure time in the initial timetable not the actual scheduled_departure_time" names the member it describes |
| `simulation/RollingStock.h:274` to `275` | "from dispatching tool". Nothing in the repository sets `dispLineID` or `arrivalPlatform`; they stay at -1, and are only written to output files (`simulation/RollingStock.cpp:568`, `606`) compared (`693`) and copied into `reservedPlatform` (`750`) |
| `simulation/RollingStock.h:263`, `268` to `270` | `numOverlaps`, `ConflictingTrains`, `N_ConflictingTrains` and `ETCS3StoppingPoint` have comments but no use beyond their initial values (`simulation/RollingStock.cpp:67`, `72`, `81`) |
| `simulation/Simulation.h:32`, `simulation/Simulation.cpp:262` | "computing time of ROMA and EGTRAIN for each combination RI-PH". `compTimeRoma` is never set outside the tests, so the column is always 0 (`simulation/Simulation.cpp:269`). [Run output](../guides/run-output.md) lists the column |
| `simulation/Simulation.cpp:450` | "probably needs also to be added the overall simulation starting time" is on a line that adds it |
| `simulation/Simulation.cpp:525` | "break the loop over the journey once the active one has been found" sits after the closing brace of the `if`, so the loop that records the waiting time of a boarding passenger ends after the first journey whether or not it is the active one |
| `simulation/Simulation.cpp:534` | "Call Route Choice Function TO BE ADDED": the only marker of unfinished work in the source |
| `app/DispatchController.cpp:700` to `702` | Advises to comment out a line and use `Update_List_Passengers_Waiting_At_Platform`, which no longer exists |
| `simulation/RollingStockTrajectory.cpp:624` | "Originally I had `IsTrainInFollowingMode = false;`" is history, not a description |
| `simulation/RollingStockTrajectory.cpp:492` to `497`, `512` to `518`, `541` to `546`; `simulation/RollingStockBlockingTimes.cpp:643` to `654`; `simulation/Signalling.cpp:1993` to `1999` | Commented-out blocks of earlier code. Single commented-out statements remain at `simulation/Signalling.cpp:302`, `306`, `390`, `1688`, `1689`, `1972`, `2447`, `simulation/RollingStockBlockingTimes.cpp:265`, `313`, `433`, `547`, `755`, `896`, `simulation/RollingStock.cpp:551` and `simulation/RollingStockOccupation.cpp:41` |
| `util/timeutil.hpp:7` to `12` | The `///` block documents a parameter `tm` that the function does not have. The comments say "CEST", but the result is a date and time without a zone (`strftime` at line 22), counted from midnight of the current day |

## Open questions

The code and its tests do not settle these. Each needs an answer from the owner of the model or from a source, not a guess in a comment.

1. Where do the constants of `traction_unit_resistences` (4.2, 0.72, 4.17, 3.6) and `wagons_resistances` (1.9, 4.8, 2.7, 4.17, 10) come from? The header names "Modified Strahl" and "Sauthoff" (`simulation/RollingStock.h:300` to `305`) but no source or unit (`simulation/RollingStockDynamics.cpp:28` to `38`).
2. What is the formula `g m 700 / (R 1000)` for curvature resistance, and is `R` meant in metres, as the builder stores it (`simulation/RollingStockDynamics.cpp:50`)?
3. Which unit do the speed bounds of a traction curve row have? The code compares them with m/s (`simulation/RollingStockDynamics.cpp:9`) and the validator gives them the range of `max_speed_ms` (`scene/SceneFields.h:57` to `59`), but the schema does not say ([Scene schema](../architecture/scene-schema.md#rolling_stockjson)).
4. Where do the eight dwell parameters `7, 0.32, 18.23, 0.564, 4.838, 22.24, 0.04, 0.562` come from, and which is which (`simulation/builders/OperationsBuilder.cpp:253`)?
5. Is `changeTrainDepartureTimesForHourlyTimetabling` needed? It runs at the end of every build (`simulation/builders/OperationsBuilder.cpp:1112`) and spaces departures over one hour, but `runSimulation` sets every `departure_time` back to `scheduled_departure_time` before the first step (`app/DispatchController.cpp:571`). Does anything read `departure_time` in between?
6. What are ROMA and "RI-PH"? Both appear in output and comments (`simulation/Simulation.cpp:262` to `269`) and in [Scene migration matrix](../guides/v1-scene-migration-matrix.md) as inactive, but are explained nowhere.
7. What is the platform booking for the stations "Alm", "Asd" and "Asdz" meant to do now that `arrivalPlatform` is never set (`simulation/RollingStock.cpp:681` to `696`, `750`)? Both sides of the comparison are -1.
8. Is it intended that the waiting time of a boarding passenger is recorded only when the active journey is the passenger's first (`simulation/Simulation.cpp:525`)? The alighting loop breaks inside the match (line 469).
9. Why does `ComputeEnergyConsumptionForAllTrains` use fixed efficiencies of 0.8 and 0.7? Its own comment says they belong to the substation and the train (`simulation/Simulation.cpp:188`).
10. Where do the 4 m standing window, the 30 m and 0.278 m/s coupling limits and the look-ahead of eight sections come from? [Signalling levels](../architecture/signalling-levels.md#what-is-not-established) already lists the coupling limits as constants of the model.
11. Can `Train::direction` go? It is assigned at `simulation/builders/OperationsBuilder.cpp:225` and never read; the code uses `Route::reversed_direction` instead.
12. Which `Eq` values besides 54 does anything outside the tests rely on, and can the model's author confirm their meanings in the conventions table? They were read from the assignments, not from a description.

## Follow-up tasks, in order

One task per subsystem. The order follows the dependencies: the scene data every part uses, then the run, then the results and the window. Each task adds documentation comments to the public headers of its subsystem, writes the conventions that the table above names where they apply, resolves the entries of the dead and misleading list for that subsystem, and records any new question instead of guessing. A task is done when Doxygen reports no undocumented public member in its headers.

1. `scene/`: `SceneModel.h`, `SceneValidator.h`, `SceneDiagnostic.h`, `SceneWriter.h`, `SceneBundle.h`, `SceneImporter.h`, `SceneExporter.h`. Units of every field, the name `gradientPercent`, the diagnostic codes.
2. `util/`: `Log.h`, `TimeFormat.h`, `SpeedFormat.h`, `CsvWriter.h`, `TrajectoryUtil.h`, `PlaybackProfiler.h`, and the wrong comment in `timeutil.hpp`. Small, and most other folders use it.
3. `simulation/` entry points: `SimulationRun.h`, `InitialParameters.h`, `RunControl.h`, `SimulationWorker.h`, `NumberGenerator.h`, the builders' public functions, and `app/DispatchController.h`. Run lifetime, the global state table, threads.
4. `simulation/` data types: `Infrastructure.h`, `Signalling.h` (types only), `RollingStock.h` (members of `Train`), `Passengers.h`. Units, sentinels, string values, fixed capacities, and the stale comments of the previous section.
5. `simulation/` behaviour: the functions of `Signalling.cpp`, `RollingStock*.cpp` and `Simulation.cpp`, in this order: the step functions, braking, occupation and blocking times. Link to [Signalling levels](../architecture/signalling-levels.md) instead of restating it. Remove or explain the commented-out blocks. Depends on the answers to the open questions.
6. `diagrams/`: `CompletedRunData.h`, `RunResults.h`, `RunExports.h`, `DelayAnalysis.h`, the capacity classes. Which numbers are seconds, metres or kWh in each row.
7. `app/`: `RunController`, `SceneSession`, `ResultViews`, `GuiSimulationSnapshot`, `GuiReplayHistory`, `RunSelection`, and the public part of `MainWindow.h`.
8. `editors/`: `SceneDocument`, `SceneEditorPanel`, `InfrastructureFacet` and the facet bases, then the panel classes.
9. `graphics/`: `NetworkPresenter.h`, `RuntimeGeometrySource.h`, `NetworkScene.h`, `NetworkView.h`, then `graphics/items/`.
10. `widgets/`: the dock widgets and dialogs, then the small controls.
11. `io/`: `LiveFrame.h`, `LiveCommand.h`, `LiveServer.h`. Link to [External state sharing](../architecture/external-sharing.md) and `docs/live/frame-v1.schema.json`.
12. `recovery/`: `RecoveryStore.h`, `RecoveryController.h`, `RecoveryPrompt.h`.
13. `update/`: `UpdateChecker.h`, `SelfUpdater.h`, `HttpTransport.h`, `UpdatePreparation.h`, `Distribution.h`, `ReleaseInfo.h`.
14. `telemetry/`: `TelemetryConsent.h`, `TelemetryQueue.h`, `TelemetrySender.h`, `TelemetryEvent.h`. Link to the [telemetry contract](../telemetry/README.md).
