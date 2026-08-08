# V1 active-input migration matrix

This matrix records the Milestone 1 boundary traced from live consumers, not
from filenames alone. `canonical` means structured V1 input, `derived` means
reconstructed from canonical topology, `output/interoperability` means it is
not authored simulation input, `dead/inert` means the normal run path does not
activate it, and `unresolved` marks a relationship that cannot yet be mapped
without guessing.

| Legacy family | Classification | V1 handling and consumer evidence |
| --- | --- | --- |
| `InitialParameters` base time, duration, buffer, recovery | canonical | `SceneModel` and `SceneWriter` use `base_time` plus the three `simulation_settings` values. `InitialParameters::set_case` supplies the known legacy-case values. Counts, UI switches, output paths, and network flags are not independent scene input. |
| `NodiCumPari`, `ArchiCumPari`, `BlockCumPari` | canonical | `Infrastructure` consumes node coordinates, arc endpoints, curvature, gradient, speed, and block-row lengths. `SceneImporter` maps them to stable track-scoped node/arc IDs and runtime-compatible block IDs. |
| `Connections.txt` | canonical | `Infrastructure` consumes track/coordinate endpoints and optional speed. The importer resolves exact coordinates to node IDs and reports zero or ambiguous matches instead of guessing. |
| track-line `Stations.txt` | canonical | `Infrastructure` assigns station anchors by exact X coordinate. V1 stores station positions and platform-to-node references; unmatched coordinates remain reported. |
| `Routes/Route<N>.txt` | canonical | `Signalling::setUpAllRoutes` consumes ordered block and switch-transition tokens. V1 routes retain that order and resolve services to stable route IDs. |
| `GUI/caseStudyRouteCorridors.txt` | canonical | `Signalling::loadRouteCorridors` consumes route/corridor rows. The importer stores the value on the corresponding V1 route; it is not treated as a second route definition. |
| `RoutesToWrite/RoutesToJoin.txt` | canonical when populated | `Signalling::loadAllJoinedRoutes` is active. The importer materializes each non-empty row as an appended route with concatenated blocks and `reversed` metadata. Committed files are empty, so they add no entities today. |
| ordinary block dependencies, signals, virtual signals, TDS | derived | `Signalling` derives these from blocks, routes, switches, and connections. They are documented derivations rather than duplicate authored inputs; the historical `signals` array remains load-compatible but is not runnable completeness. |
| Copenhagen `5-B6` dependency exception | canonical | The live signalling path adds a non-derivable dependency from `5-B6` to `@1-B30@-4.592000/@5-B7@-4.620000`. V1 persists that explicit exception and validates each composite component. |
| `GUI/singleTrackLimits.txt` | canonical | The active reader consumes four distinct block roles. V1 stores `start_block`, `end_block`, `protected_start_block`, and `protected_end_block` explicitly. |
| `GUI/stationBoundarySections.txt` | canonical | The active reader consumes entrance block, optional exit block, and direction. V1 stores those fields directly. |
| seven-token `Trains` definitions | canonical | `RollingStock::LoadAllTrainFiles` consumes operating code, entry, headway, route index, physical file, traction file, and timetable file. The importer preserves every explicit relationship. Canonical service IDs remain unique while `operating_code` retains duplicate active codes such as Milano-Brescia `9707` and `9709`. |
| train-unit physical files | canonical | `RollingStock::loadTrainType` consumes nine physical/operating values. V1 stores them on a generic train unit, independent of `LITRA` terminology. |
| tractive-effort files | canonical | The runtime consumes five-number curve rows. The train manifest, not `T_` or another filename convention, associates a curve with its train unit. |
| compositions | canonical | V1 compositions contain ordered train-unit references. Current legacy manifests yield one-unit compositions, without making the source filename a universal train type. |
| planned timetable arrival, departure, dwell, repetition | canonical | Runtime timetable and train setup consume these values. V1 uses independently optional `planned_arrival_seconds` and `planned_departure_seconds`; `-1` becomes absence and inconsistent source values are reported without alteration. |
| flat `Incidents.txt` / historical `incidents.json` | canonical compatibility | The active incident loader supports signal failures and train breakdowns. Import places flat incidents in the baseline and copies them into each imported rollout scenario because the legacy runtime applies both inputs together; preferred V1 persistence is `scenarios.json`. |
| `Rollout_<n>.txt` entrance delays | canonical when resolvable | The active Paimpol path applies the positional vector to service occurrences at Guingamp/Paimpol. The importer maps it only with a known duration and headway; count mismatches or unknown case timing are reported rather than guessed. No committed `.txt` rollout currently exists. |
| DAS plus RouteChoice passenger CSVs | canonical | `DispatchController` activates passenger loading only when both exact files exist. V1 stores absolute-midnight journey windows and ordered service/occurrence legs; randomized draws and simulated passenger results are excluded. |
| generated `TrackandStations`, `VisualizeConnections`, `ShowElements`, `List_of_Blocks` | derived/output | Infrastructure and signalling produce these display and route-authoring aids from topology. They are not additional authoritative scene inputs. |
| `TrackLines/AreasCaseStudy.txt` and historical `Areas*.TXT` | dead/inert in the current source | No current simulation reader consumes these files. The legacy exporter still preserves or synthesizes `AreasCaseStudy.txt` for compatibility with older variants, but it is not a second V1 signalling model. |
| `Draisy-acceleration.txt` | dead/inert | No normal runtime consumer references this related file; only the explicitly selected physical and traction paths are loaded. It therefore does not justify another canonical data type. |
| `TimeTable/Scenarios_DW*` | dead/inert | The dwell-disturbance loader call in `DispatchController` is commented out. Directory presence alone does not activate it. |
| OL train-order lists | dead/inert | `DispatchController` resets `N_OrderLists` to zero before the loader loop, and the `Set_RespectOrder` activation calls are commented. Committed uppercase `.TXT` names also differ from the lowercase lookup on case-sensitive systems. |
| normal `Rescheduling`, ROMA, and legacy TMS/TDS input trees | dead/inert or derived | ROMA closed-loop loading is not active in normal setup, and TDS is built from topology. Files remain compatibility passthrough where needed, not canonical fields without a live consumer. |
| `EGTRAINOutput`, ROMA/TDS artifacts, `FolderEGTRAIN`, `OL_LastEntry`, `ScheduledOrder` | output/interoperability | Simulation and integration code write these artifacts. They are not case-scene input. |
| GUI coordinate/display files other than the explicit corridor/restriction/boundary inputs | output/interoperability | Geocoding and the GUI consume them for display; simulation station X/name values come from track-line data. They remain view/compatibility data rather than a second infrastructure model. |
| RailML/XML | output/interoperability | `RailMLParser` serializes traffic state and route choice for exchange; it does not import a V1 scene. |
| `rand1.seed` | output/interoperability | `NumberGenerator` persists mutable process RNG state. It is neither authoritative case input nor a passenger result field in V1. |
| unknown-case compiled `InitialParameters` mapping | unresolved | The four canonical timing fields are known, but a folder/name outside the explicit case table cannot safely select a `set_case` branch. Import leaves them absent and records the unresolved mapping. |

## Known source ambiguities

- Copenhagen contains a Koge Nord stop whose planned arrival is later than its
  planned departure. Import preserves both source values, reports the row, and
  lets semantic validation reject the ordering instead of silently repairing it.
- The committed Paimpol passenger source has 376 DAS journeys but only 10
  route-choice rows, producing 14 resolved legs. The remaining 366 journeys are
  retained without legs and reported. Thirty DAS station references and three
  route-choice service or station references remain unresolved rather than being
  matched heuristically.
- No committed `Rollout_<n>.txt` exists. The importer supports its known
  positional relationship only where case duration and service headway make the
  occurrence mapping explicit.
- Legacy cases outside the compiled `InitialParameters` case table have no
  reliable timing association. Their V1 timing fields remain absent and the
  import report records the unresolved mapping.

The compatibility `legacy/` tree remains because the current simulator still
uses legacy loaders. Its presence does not make every copied file canonical,
and removing that runtime path belongs to a later milestone.
