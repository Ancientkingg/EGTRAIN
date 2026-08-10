# V1 active-input migration matrix

This matrix records the V1 boundary traced from live consumers, not from
filenames alone. The Milestone 4 runtime consumes canonical infrastructure and
operations through one native setup path. Legacy readers remain only behind the
explicit importer/exporter boundary. `canonical` means structured V1 input, `derived` means
reconstructed from canonical topology, `output/interoperability` means it is
not authored simulation input, `dead/inert` means the normal run path does not
activate it, and `unresolved` marks a relationship that cannot yet be mapped
without guessing.

| Legacy family | Classification | V1 handling and consumer evidence |
| --- | --- | --- |
| `InitialParameters` base time, duration, buffer, recovery | canonical | `SceneModel` and `SceneWriter` use `base_time` plus the three `simulation_settings` values. The native operations builder applies these values and derives service-line/station reporting maps; UI switches, output paths, and network flags remain runtime configuration. Numeric case shortcuts now choose scene directories, not compiled input-folder tables. |
| `NodiCumPari`, `ArchiCumPari`, `BlockCumPari` | canonical | `SceneImporter` maps node coordinates, arc endpoints, curvature, gradient, speed, and block-row lengths to stable track-scoped node/arc IDs and runtime-compatible block IDs, including the UTF-16LE block files supplied by Lebanon. The normal runtime consumes only the resulting canonical model. |
| `Connections.txt` | canonical | `SceneImporter` resolves track/coordinate endpoints and optional speed to canonical node IDs and reports zero or ambiguous matches instead of guessing. |
| track-line `Stations.txt` | canonical | `SceneImporter` maps exact station coordinates to V1 station positions and platform-to-node references; unmatched coordinates remain reported. |
| `Routes/Route<N>.txt` | canonical | `SceneImporter` converts ordered block and switch-transition tokens. The native scene builder consumes the resulting V1 route order and resolves services to stable route IDs. |
| `GUI/caseStudyRouteCorridors.txt` | canonical | `SceneImporter` stores each route/corridor row on the corresponding V1 route; it is not treated as a second route definition. |
| `RoutesToWrite/RoutesToJoin.txt` | canonical when populated | `SceneImporter` materializes each non-empty row as an appended route with concatenated blocks and `reversed` metadata. Committed files are empty, so they add no entities today. |
| ordinary block dependencies, signals, virtual signals, TDS | derived | `Signalling` derives these from blocks, routes, switches, and connections. They are documented derivations rather than duplicate authored inputs; the historical `signals` array remains load-compatible but is not runnable completeness. |
| Copenhagen `5-B6` dependency exception | dead/inert | The hard-coded target names track `B30`, but the switch at `4.592/4.620` is generated from `B31` and `B7`. No runtime section has the hard-coded ID, so occupancy and node-connection scans never resolve it. V1 does not turn that stale string into an active dependency or guess a replacement by coordinates. |
| `GUI/singleTrackLimits.txt` | canonical | `SceneImporter` converts four distinct block roles to `start_block`, `end_block`, `protected_start_block`, and `protected_end_block`. The native builder consumes those V1 fields directly. |
| `GUI/stationBoundarySections.txt` | canonical | `SceneImporter` converts entrance block, optional exit block, and direction; the native builder consumes those V1 fields directly. |
| seven-token `Trains` definitions | canonical | The native operations builder consumes canonical service/composition links, entry, repetition, and timetable values without opening provenance files. It gives occurrences stable `(service id, 1-based occurrence)` identities and the existing `<service id>-<occurrence>` runtime key. The seven-token form is now read only by the explicit importer. |
| train-unit physical files | canonical | `SceneImporter` converts nine physical/operating values to a generic V1 train unit, independent of `LITRA` terminology. The native operations builder consumes those values in memory. |
| tractive-effort files | canonical | `SceneImporter` converts five-number curve rows. The train manifest, not `T_` or another filename convention, associates a curve with its train unit; the native operations builder consumes the canonical curve. |
| compositions | canonical | V1 compositions contain ordered train-unit references. Current legacy manifests yield one-unit compositions, without making the source filename a universal train type. |
| planned timetable arrival, departure, dwell, repetition | canonical | Native operations expands repeats as `ceil(duration/headway)` when `repeat.count` is omitted, or as the explicit positive total count (including the base). Entry and stop offsets remain `(occurrence-1)*headway`; absent arrival/departure values remain runtime `-1`, including repeats. Finite negative planned values remain valid for pre-horizon timetable rows; route-external rows remain inert without an invented platform. A nonzero `operating_code_step` requires a decimal base and yields stepped occurrence codes; without one, repeated codes use a readable base-plus-occurrence form. `departure_time` is a derived compatibility value from the existing hourly-retiming algorithm; it is not canonical input. |
| service performance and speed controls | canonical | `performance_percent` defaults to 100% and is limited to finite `1..100`; `maximum_speed_kmh` is an optional positive finite service cap. Native runtime applies `min(composition max, service cap)` first, then performance once; 100% preserves the raw path. Braking, mass, shared compositions, buffer, and recovery remain unchanged. Each `Train` and `TrainRunResult` retains operating code, service/occurrence identity, performance, configured cap, composition maximum, and applied maximum-speed provenance. |
| run occurrence selection | derived/runtime | `SceneRunSelection` is a set of `(service id, occurrence)` identities. Empty means all; a non-empty selection is validated, limits capacity to selected trains, and omits delays/passenger legs for excluded occurrences while leaving `SceneModel` untouched. `DispatchController::prepareScene` and the native operations builder share this selection boundary. |
| flat `Incidents.txt` / historical `incidents.json` | canonical compatibility | Import places flat incidents in the baseline and copies them into each imported rollout scenario where the source relationship is explicit. Preferred persistence and native execution use `scenarios.json`. |
| `Rollout_<n>.txt` entrance delays | canonical when resolvable | Native operations maps canonical service/occurrence/station delays, preserves absent timetable values as errors rather than numbers, and applies only the selected scenario. Positional rollout files are importer input only. |
| DAS plus RouteChoice passenger CSVs | canonical | Native operations maps canonical windows and ordered service/occurrence legs directly to existing `Passenger`, `Journey`, and `Trip` objects, including legless journeys; actual planned times are sampled in memory. The CSV pair is read only during explicit import. |
| generated `TrackandStations`, `VisualizeConnections`, `ShowElements`, `List_of_Blocks` | derived/output | Infrastructure and signalling produce these display and route-authoring aids from topology. They are not additional authoritative scene inputs. |
| `TrackLines/AreasCaseStudy.txt` and historical `Areas*.TXT` | dead/inert in the current source | No current simulation reader consumes these files. The legacy exporter still preserves or synthesizes `AreasCaseStudy.txt` for compatibility with older variants, but it is not a second V1 signalling model. |
| `Draisy-acceleration.txt` | dead/inert | No normal runtime consumer references this related file; only the explicitly selected physical and traction paths are loaded. It therefore does not justify another canonical data type. |
| `TimeTable/Scenarios_DW*` | dead/inert | The dwell-disturbance loader call in `DispatchController` is commented out. Directory presence alone does not activate it. |
| OL train-order lists | dead/inert | Native operations leaves order lists disabled and sets `N_OrderLists` to zero, matching the traced inactive path. `DispatchController` resets it before the legacy loader loop, and the `Set_RespectOrder` activation calls are commented. |
| normal `Rescheduling`, ROMA, and legacy TMS/TDS input trees | dead/inert or derived | ROMA closed-loop loading is not active in normal setup, and TDS is built from topology. Files remain compatibility passthrough where needed, not canonical fields without a live consumer. |
| `EGTRAINOutput`, ROMA/TDS artifacts, `FolderEGTRAIN`, `OL_LastEntry`, `ScheduledOrder` | output/interoperability | Simulation and integration code write these artifacts. They are not case-scene input. |
| GUI coordinate/display files other than the explicit corridor/restriction/boundary inputs | derived/output | The native GUI derives its fallback layout from canonical node and station coordinates. Historical geocoding, hidden-track, virtual-link, and HTML-template files are not a second infrastructure model and are no longer normal runtime input. |
| RailML/XML | output/interoperability | `RailMLParser` serializes traffic state and route choice for exchange; it does not import a V1 scene. |
| `rand1.seed` | output/interoperability | `NumberGenerator` persists mutable process RNG state. It is neither authoritative case input nor a passenger result field in V1. |
| unknown-case compiled `InitialParameters` mapping | unresolved | The four canonical timing fields are known, but a folder/name outside the explicit case table cannot safely select a `set_case` branch. Import leaves them absent and records the unresolved mapping. |

## Resolved source corrections and remaining ambiguities

- Copenhagen source service `E-Holte-Koge_2` records Koge Nord arrival at 4660
  seconds and departure at 4460 seconds. The prior canonical scene corrected
  departure to 4660; the native conversion retains and documents that correction.
- Paimpol's `Draisy-traction.txt` contains two overlapping three-band alternatives.
  The prior validated scene retained the second group; the native conversion does
  the same and records three converted plus three skipped rows.
- Paimpol B4 arc 108 incorrectly restarts at node 1 after the source's ordered
  1→2→…→9 chain. V1 corrects it to 9→10, consistent with the ordered nodes,
  neighboring B0/B1 track files, and the runtime's linear TrackLine model.
- The Paimpol passenger source has 376 DAS journeys and 10 route-choice rows.
  The exact `Tregonnau Squiffiec` source alias maps to canonical
  `Tregonneau_Squiffiec`, and `Guin-Paim_EXPRESS-1-1` maps to service
  `Guin-Paim-EXPRESS-1`, occurrence 1. Two reverse-service tokens have no active
  service definition; their legs are omitted, the journeys remain, and the import
  report records two unresolved references. The committed result contains 12
  resolved legs.
- Six Netherlands `virtual*Link` station markers have no exact platform-node
  anchor. They are retained as stations without platforms and reported; no service
  stop refers to them.
- Netherlands block `0-B265` was the sole source block with a negative length
  (`-1.597` km) even though its nodes and arcs run in increasing coordinate order.
  V1 corrects the sign while retaining the source magnitude; the standard
  final-block extension covers the remaining 20 metres of track geometry.
- No committed `Rollout_<n>.txt` exists. The importer supports its known
  positional relationship only where case duration and service headway make the
  occurrence mapping explicit.
- Legacy cases outside the compiled `InitialParameters` case table have no
  reliable timing association. Their V1 timing fields remain absent and the
  import report records the unresolved mapping.

The committed scenes no longer contain compatibility `legacy/` trees, and the
obsolete committed `Input/` cases are removed after conversion parity. Their
filenames remain only as compact provenance in the import reports.
