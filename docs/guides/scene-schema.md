# V1 Scene Schema Reference

V1 is a directory of JSON files. JSON keys are case-sensitive. The writer
emits the preferred keys below; the historical spellings are accepted only
for compatibility and are listed in [Historical compatibility aliases](#historical-compatibility-aliases).

## Version and files

`scene.json.schema_version` is the integer `1`. Structural loading requires:

| File | Required | Preferred root and purpose |
| --- | --- | --- |
| `scene.json` | yes | scene metadata and simulation settings |
| `infrastructure.json` | yes | tracks, nodes, arcs, blocks, and connections |
| `stations.json` | yes | stations and platforms |
| `signalling.json` | yes | signals, routes, and signalling relationships |
| `rolling_stock.json` | yes | train units and compositions |
| `services.json` | yes | services and planned timetables |
| `scenarios.json` | no | `default_scenario_id` and named scenarios; `SceneWriter` always writes it |
| `passengers.json` | no | passenger journeys and legs |
| `views.json` | no | display preferences; not simulation input |

`legacy/` is not part of the runnable V1 contract. The importer may read a
legacy case directory, and the exporter may write one when explicitly asked,
but normal loading and simulation use only the canonical files above.

For a V2 `.egscene` bundle, `scenarios.json` is required; `passengers.json` and
`views.json` are optional entries. `legacy/`, assets, results, and unknown or
nested ZIP entries are excluded; see
[V2 Transparent Scene Bundle](scene-bundle.md).

## `scene.json`

Required keys:

- `schema_version`: integer `1`.
- `name`: non-empty string.

Optional keys:

- `description`: string.
- `base_time`: `HH:MM:SS` day-clock origin from `00:00:00` through `23:59:59`.
- `units`: object containing only `distance: "m"`, `time: "s"`, and
  `speed: "m/s"` when present.
- `simulation_settings`: object with optional numeric
  `duration_seconds`, `buffer_time_seconds`, and
  `recovery_time_percent`.
- `import_report`: array of conversion rows.

Inside a bundle, `format` is the required string `"egscene"` and
`bundle_version` is the required integer `1`. They are optional transport
metadata for directory scenes and do not change the V1 data schema.

An `import_report` row has `category`, `source_count`, `converted_count`,
`skipped_count`, and `unresolved_references`; `source_file` is optional. The
counts describe the conversion source, not simulation results.

Counts, GUI switches, output paths, and network feature flags from legacy
[`InitialParameters`] are not V1 scene keys. `scene_tool export` stores the base
time and the simulation settings in `EGTRAINInitialParameters.json` with the keys
of `scene.json`, and the legacy import reads that file.

## `infrastructure.json`

The preferred root keys are `tracks`, `nodes`, `arcs`, `blocks`, and
`connections`. `nodes` and `arcs` are required arrays; the other arrays may be
empty or omitted on input and are emitted by the writer.

- `tracks[]`: `{ "id": string }`.
- `nodes[]`: `id`, `track`, numeric `x_km`, numeric `y_km`.
- `arcs[]`: `id`, `track`, `from`, `to`, numeric `curvature_radius_m`,
  `gradient_percent`, and `speed_limit_ms`. The simulation uses
  `gradient_percent` unchanged as rise per length: `0.01` means a 1% slope.
  The key keeps its legacy spelling for compatibility. Loading, editing and
  saving do not rescale the value; the editor labels it "Gradient (rise/length)".
- `blocks[]`: `id`, `track`, numeric `length_km`. Block IDs cannot contain `/`,
  which is reserved as the separator in connection-derived section identities.
- `connections[]`: `id`, `from`, `to`, and optional numeric `speed_limit_ms`.

## `stations.json`

The root key is `stations`. Each station has required string `id` and `name`,
optional numeric `position_km`, and optional `platforms`. A platform has an
`id`, an optional `nodes` array of node IDs, and optional positive finite
`length_m` and `width_m` values. Missing dimensions retain the V1 compatibility
defaults of 100 m by 2.5 m. The passenger runtime derives integer platform
capacity from the effective dimensions and rejects values that produce less
than one passenger or exceed its integer capacity field.

## `signalling.json`

Required root arrays are `signals` and `routes`.

- `signals[]`: required string `id` and optional string `protected_section`.
  `protected_section` names one exact derived runtime section (a base block
  alias is accepted for V1 compatibility); it is omitted when empty. Signal
  failures resolve through this binding, while direct base-block incident
  targets remain supported. A signal-failure target that matches both a signal
  ID and a section reference is ambiguous and invalid.
- `routes[]`: required `id` and string-array `blocks`; optional string
  `corridor` and boolean `reversed`. A route runs in reverse when its sections
  are listed in reverse order. Validation reports `scene.route.direction` for a
  route marked `reversed` whose sections run forward.
- `signalling_areas[]`: required `id`, numeric `start_km`, numeric `end_km`,
  and integer `level` from 0 through 5; optional `track` refers to a canonical
  track ID. The array is optional. The editor writes -99999999 as the level of an
  area whose system is not chosen yet; the loader reads the number and
  validation reports it.
- `block_dependencies[]`: `{ "block": string, "depends_on": string }`.
- `single_track_restrictions[]`: preferred explicit
  `start_block`, `end_block`, `protected_start_block`, and
  `protected_end_block`. See [Single-track restrictions](#single-track-restrictions).
- `station_boundaries[]`: required `entrance_block`, optional `exit_block`,
  and optional boolean `direction`.

A signalling area covers a native section only when the section's complete
coordinate span lies inside the area. The span is compared with the chainage of
the section's own track, and each edge has a tolerance of 1e-8 km. A section
that an area edge cuts is not covered by that area. Network-wide areas are applied before
track-scoped areas; a track-scoped value overrides the network-wide value.
Either connected track matches a derived switch section. Different values at
the same precedence tier that cover one section are invalid. The builder
applies areas after base and switch sections exist and before routes copy them.
A section with no matching area retains the unset signalling value. The loader
and writer never create a default level. Runnable validation reports the route
sections without a level in one `scene.signalling.level.missing` warning;
sections that are on no route are not reported. When the scene has areas, the
warning's suggested fix lists the stretches of each track without a level and
the areas next to each.

Area diagnostics name the area and give the values behind them:

- `scene.signalling_area.range` (error): the message shows `start_km`, `end_km`
  and the extent of the blocks of the network or of the area's track.
- `scene.signalling_area.level` (error): the message shows the level that lies
  outside 0 to 5. An area without a chosen system gets the message "has no
  signalling system" instead.
- `scene.ref.unresolved` on `signalling_areas[].track` (error): the message
  names the unknown track and lists the tracks of the network.
- `scene.signalling_area.conflict` (error, runnable validation): one error for
  each pair of areas that give sections different levels, with the number of
  sections, the first one, its km range and the name of both levels.
- `scene.signalling_area.splits_section` (warning, runnable validation): one
  warning for each area edge that lies inside a route section, naming the
  section and its km positions, because that section is not part of the area.
- `scene.signalling_area.empty` (warning, runnable validation): the area
  contains no complete section; the message shows the extent of the blocks.

What each level does, and how signalling areas differ from block sections, is in
[Signalling levels](signalling-levels.md).

Validation reports the arcs of a route that its trains cannot brake or start on
in one `scene.route.gradient.steep` warning per route and composition that a
service uses. The braking limit of a composition is
`mass_factor * max_deceleration_ms2 / g`, applied to descents. The starting
limit is the tractive effort at 0 m/s divided by `g * total_train_mass`,
applied to ascents; other resistances are ignored. Both limits are in the unit
of `gradient_percent`. A route driven in reverse sees the opposite sign of each
stored value. The warning names the steepest arcs with their stored values. A
route whose direction cannot be resolved is compared by absolute value with the
smaller limit.

Section references are resolved from the transient section inventory derived
from the authored tracks, blocks, arcs, and connections. Route order is
authored order. Adjacent sections must remain contiguous and keep one direction
inside a connected region. Scenes carrying legacy-import provenance retain
their historical jumps between different regional coordinate systems with a
validation warning; newly authored scenes require a declared connection.
Unknown or malformed
compound section tokens are invalid. The inventory is not persisted as a
second topology model.

### Single-track restrictions

A restriction holds a single-track stretch for one direction. `start_block` and
`end_block` are the first and last plain sections of the stretch, in either order. `protected_start_block` and
`protected_end_block` are the sections at its two ends where a passing loop
joins it. The stretch consists of these four sections and every section between
`start_block` and `end_block` on a route that contains both. A route is affected
by the sections of the stretch that it contains.

While a train is in the stretch, trains on routes of the opposite direction see
all its sections as occupied and wait in front of it, at the loop if their route
has one. Trains of the same direction are not held and follow under the normal
signalling rules. The direction of a train follows the order of the sections of its
route; the optional `reversed` flag has to agree.
If trains of both directions are already inside, the previous holder keeps
priority; without a previous holder, the forward direction wins. Pending
reservations can keep the stretch held after the last occupant leaves.

A free stretch is reserved before an approaching train's next movement could
consume its last opportunity to brake. The calculation uses the existing inverse
braking curve and an upper bound on next-step speed from the train's starting
tractive effort, mass, resistances and current arc. It includes the approach
speed targets of fixed-block signalling. A stationary train can ask when it could
accelerate into this braking window. A due route entry asks without trajectory
samples. One requesting direction reserves the stretch; simultaneous requests
on a free stretch go to the forward direction. Occupied holders keep priority.

Pending entries retain the reservation through slowing, dwell, breakdowns and
other temporary stops. Same-direction followers can commit before their leader
clears. Passage, termination, removal or a changed route, destination or entry
identity cancels that owner's reservation. A failed or non-finite braking lookup,
or a braking point before the route origin, requests conservatively rather than
assuming that the train can wait until later.

At signalling levels 0 to 5 each route against the holder gets an end of authority
at the end of the section before each signalled stretch on that route. If the
stretch starts at the route origin, the authority is at that origin. The authority
uses the existing direction filter, so the holder and same-direction followers
ignore it. The opposing train stops and continues when the stretch is released.
Fixed-block aspects remain in effect too. Without a signalling level the
restriction does not keep opposing trains apart. A train that has to wait needs
a route that gives it somewhere to wait outside the stretch, such as a loop.
On a through route without a loop it stops on the track that the other train
needs; a reservation prevents zone overlap but cannot create a passing place.

A stub track is a track that trains enter and leave at one end. It has no loop at
its closed end, so a restriction over a stub gives `protected_end_block` the same
section as `end_block`. A section named in two roles is one section of the
stretch. A train of the other direction waits at the entry of its route when the
first section of its route lies in the held stretch, and in front of the stretch
otherwise. The stretch is free again when the last train of the holding direction
has left it. At level 0, without the restriction, trains of opposite directions
on a stub pass through each other, or stand head to head until the end of the run
when they meet at a block edge.

Runnable validation reports each restriction whose start, end or protected
blocks have no signalling level in one `scene.single_track.no_effect` warning.
The warning names the restriction by its index in `single_track_restrictions`
and lists the blocks that have no level.

## `rolling_stock.json`

Required root arrays are `train_units` and `compositions`.

Each train unit has an `id` and may have:

- `physical`: all nine numeric keys
  `mass_of_traction_unit_kg`, `mass_of_a_wagon_kg`, `number_of_wagons`,
  `max_speed_ms`, `max_deceleration_ms2`, `frontal_area_m2`,
  `resistance_coefficient`, `jerk_ms3`, and `length_m`.
- `traction_curve`: an array of five-number rows `[v_lower, v_upper, c0, c1, c2]`.
- `source`: an object whose independent string keys are `data_file` and
  `traction_file`.

The two source paths are explicit relationships. A `data_file` does not imply
a matching name, prefix, or `T_` file; the same applies in the other direction.

Each composition has `id` and a string-array `units` of train-unit IDs.

## `services.json`

The root key is `services`. Each service has required `id`, `composition`,
`route`, and `stops`. The ID is the unique cross-file reference and remains
the stable service identity even when the service is repeated. `operating_code`
is optional and defaults to the service ID; it may be shared by distinct
services. Runtime occurrence identity is the pair
`(service_id, occurrence)`, with a 1-based occurrence number, and its existing
display key is `service_id-occurrence`.

`category` is optional string metadata in schema V1. Missing or empty means no
category; the writer omits empty values. The editor offers Intercity, Regional,
High speed/international, Freight, Metro/urban and Suburban, and preserves
unknown strings exactly. Category does not affect simulation or inferred train
display types. Folder and bundle saves preserve it; legacy export omits it
because the old format has no category field.

`visualization_color` is an optional string in schema V1 that sets a display
colour for the service. The accepted form is `#RRGGBB`, with six hexadecimal
digits in either case, for example `#3C8DD2`. The loader keeps the text as
written. It is for display only and does not affect the simulation. The writer
omits it when unset, so a scene without it is saved unchanged. Folder and
bundle saves preserve it; legacy export omits it because the old format has
no field for it. Any other non-empty text gives the validation warning
`scene.service.color.invalid`, and the default train colour is used for that
service.

Optional runtime controls are:

- `performance_percent`: percentage, default `100.0`, finite range `1..100`.
  It scales available tractive effort and the commanded maximum speed, but not
  braking, mass, composition data, recovery, or buffer settings.
- `maximum_speed_kmh`: positive finite speed in km/h. It is a service cap and
  is applied as `min(composition max speed, service cap)` before performance
  scaling. The writer omits it when it is not configured.
- `entry_time_seconds`: finite non-negative seconds relative to the scene
  base-time origin. The historical boolean `through` remains readable and
  writable, but the stop list determines scheduled calls.
- `repeat`: an object with positive finite `headway_seconds` in seconds. Its
  optional positive integer `count` is the total number of occurrences,
  including the base occurrence, and overrides the duration/headway horizon.
  When omitted, the horizon remains `ceil(duration_seconds / headway_seconds)`.
  Stop and entry offsets remain `(occurrence - 1) * headway_seconds`.

`repeat.operating_code_step` is optional, nonzero, and valid only when the
  base operating code is decimal digits. It adds the step for each occurrence
  (for example `1723`, `1725`, `1727` with step `2`). Invalid stepped input is
  rejected. Without a step, repeated services expose a readable
  `base-operating-code-occurrence` code. The canonical service ID never
  advances or changes. Run selection uses a set of `(service_id, occurrence)`
  identities; an empty set builds all occurrences, while a non-empty set must
  name occurrences inside each service's horizon.

Each stop has required string `station` and numeric `dwell_seconds`, plus an
optional string `platform`. The planned timetable keys are:

- `planned_arrival_seconds`: optional numeric planned arrival.
- `planned_departure_seconds`: optional numeric planned departure.
- `dwell_seconds`: required finite non-negative planned dwell in seconds.

Arrival and departure are independently optional on every stop. There is no
last-stop-only departure rule. Legacy timetable `-1` values are preserved as
missing planned fields; the importer does not synthesize a value from the
other field. Validation may warn when an intermediate schedule is incomplete.
These are input plans in seconds relative to the scene's base-time origin, not
simulation results.

A train stands at a stop for `dwell_seconds`, counted from its arrival in steps
of one second, and also until the planned departure of the stop when it has
one. Boarding and alighting passengers can make the stop longer, never shorter:
the stop lasts the passenger-dependent dwell when that is longer than
`dwell_seconds`. The passenger-dependent dwell is 0 when nobody boards or
alights, so a stop without passengers lasts exactly its planned dwell, in a
scene without passengers as well. Otherwise it is a fixed time plus the time of
the busiest door of the train. A door takes a boarding time and an alighting
time, both counted per passenger, so a door that nobody uses takes no time
([`Train::computePaxDependentDwellTimeAtStations`], after Fernandez et al. 2007
and Gibson et al. 1989).

An empty `stops` array means no scheduled calls without requiring `through`.
Nonempty stops take precedence over `through: true`. Legacy export writes the
actual stops, or an empty timetable for no calls; legacy import marks an empty
timetable as through.

Planned route events must be finite, non-negative, ordered and no earlier than
explicit entry. Departure before arrival blocks Run. Inert off-route context
can retain finite historical negative offsets. Missing intermediate departures
and dwell exceeding the arrival/departure window are advisory warnings, not
physical running-time predictions.

## `scenarios.json`

The preferred root keys are `default_scenario_id` and `scenarios`. The default
ID selects one named scenario. Each scenario has required `id`, `name`, and
`incidents` array; it may have `description` and `entrance_delays`.

Each incident is concrete input with `id`, `type` (`signal_failure` or
`train_breakdown`), `target`, and finite non-negative `start_seconds`.
`end_seconds` is explicitly optional in the JSON shape, but is required for
signal failures and legacy full-hold breakdowns. A breakdown with
`reduced_speed_kmh` may omit `end_seconds`; its positive finite speed cap then
continues until the target reaches its normal route end. When present, an end
is recovery time and the breakdown interval is `[start_seconds, end_seconds)`.
The optional breakdown fields are:

- `occurrence`: positive 1-based service occurrence. When omitted, the
  breakdown targets every occurrence of the canonical `target` service.
- `reduced_speed_kmh`: positive finite effective-trajectory speed cap. It
  changes neither composition data nor the canonical service maximum.
- `terminate_at_destination`: boolean request retained after recovery; the
  result is marked when that occurrence reaches its authored route end.

The occurrence must be inside the service's configured repeat range. Signal
failures accept only the legacy target/start/end fields. An entrance delay has
`service`, optional integer `occurrence`, `station`, and numeric
`delay_seconds`.

The four-column legacy `Incidents.txt` exporter cannot represent an enhanced
breakdown (occurrence, reduced speed, destination termination, or an omitted
end), so it skips that row with a compatibility diagnostic instead of
exporting full-hold semantics.

If `scenarios.json` is absent, loading creates a baseline scenario. A legacy
`incidents.json` is read into that baseline for compatibility. Saving writes
the scenario model to `scenarios.json` and removes the stale flat file.

### Standalone scenario JSON

The scenario editor's import/export boundary is one scenario object, not a
scene bundle and not the `scenarios.json` wrapper:

```json
{
  "id": "signal-failure",
  "name": "Signal failure",
  "description": "Optional notes",
  "incidents": [
    {"id": "incident-1", "type": "signal_failure", "target": "signal-1",
     "start_seconds": 300, "end_seconds": 600}
  ],
  "entrance_delays": [
    {"service": "service-1", "occurrence": 1, "station": "station-1",
     "delay_seconds": 30}
  ]
}
```

`description` and `entrance_delays` are optional; the other fields shown are
the scenario-only contract. Infrastructure, services, rolling stock, and
results are deliberately excluded. The same scenario serializer is used for
the entries inside `scenarios.json`, so incident and entrance-delay values do
not diverge between the two forms. The editor checks incident types/targets
and entrance-delay references against the open scene before importing. A
conflicting scenario or incident ID is never used as-is; the imported object
is added with a reported unique ID.

An occurrence-specific reduced-speed breakdown can instead be authored as:

```json
{"id": "breakdown-2", "type": "train_breakdown", "target": "service-1",
 "start_seconds": 900, "occurrence": 2, "reduced_speed_kmh": 40,
 "terminate_at_destination": true}
```

### Delay comparison provenance

The results UI can freeze a completed incident-free, no-entrance-delay run as
the delay baseline, and the delay analysis window compares a later run with it.
Baseline and scenario results are value snapshots, not runtime train pointers.
The two runs must agree on scene revision, base time, duration, timestep and
the selected `(service_id, occurrence)` identities. The window reports the
difference and the recorded direct incident evidence, and never states a cause.
The checks, formulas and limits are in
[How delays are calculated](delay-analysis.md#delay-comparison-against-a-baseline).
The incident IDs, first direct time and location, and termination outcome of
each train are in `run_summary.csv`.

## `passengers.json`

This file is optional. Its root key is `passengers`. Each passenger has `id`
and `journeys`. A journey has `id`, `origin`, `destination`, optional
`activity`, required `planned_departure` and `planned_arrival` objects, and a
`legs` array. Each time-window object uses numeric `start_seconds` and
`end_seconds`, expressed as absolute seconds from midnight, not seconds from
`base_time`. A journey with an empty activity is written without the `activity`
key. Each leg has `id`, `origin`, `destination`, `service`, and an optional
integer `occurrence` that defaults to `1`.

A leg's destination must follow its origin in the referenced service's stop
order. Repeated stations use the first ordered matching pair. A non-empty run
selection stages a passenger journey only when all of its legs are selected;
it never constructs a partial journey. Legacy-import provenance downgrades a
historical reverse leg to an actionable warning and native staging omits the
whole affected journey.

Journey and leg IDs are stable across the scene. A journey may have an empty
`legs` array when the active legacy route-choice file has no row for it; this
is reported as a warning rather than inventing a route.

Random draws and simulation results are not scene input. The explicit legacy
importer recognizes passenger data only when both exact source files
`Passengers/DAS_FrenchCaseStudy.csv` and
`Passengers/RouteChoiceFC_EQ1.csv` exist. Once converted, the native runtime
uses `passengers.json` and does not reopen those CSVs. The passenger editor can
append records from the same exact file pair. Its transient result table reports
accepted, unresolved, malformed, and ID-collision rows before the scene is saved.

## `views.json` and runtime metadata

`views.json` is optional display metadata in directories and bundles. It does not change
topology, signalling, services, or simulation results:

```json
{
  "tracks": [
    { "track": "B0", "level": 0, "region": 0, "visible": false }
  ],
  "stations": [
    {
      "station": "Frederikssund",
      "latitude": 1.0,
      "longitude": 0.0017,
      "regions": [{ "id": 0, "position_km": 0.17 }],
      "corridors": []
    }
  ]
}
```

Track rows are visible by default. Set `visible` to `false` to keep infrastructure
available to the simulation while omitting an authored inactive track from the preview.

Track and station references must identify canonical objects. Levels are
integers; region IDs are unique non-negative integers; coordinates and region
positions are finite. Invalid rows are skipped with load warnings. The legacy
importer creates this file from `GUI/caseStudyTrackData.txt`, `GUI/unusedTracks.txt`,
and `GUI/StationsCoord.txt`. Directory saves and V2 bundles preserve it when present.

`loaded_data` and `sourceFiles` are derived in-memory metadata; they are
recomputed when a scene is loaded and are not the source of canonical values.

## Historical compatibility aliases

The loader accepts these old spellings or files when the preferred form is
absent. New files should use the preferred form, which is what `SceneWriter`
emits.

| Historical form | Preferred V1 form | Compatibility behavior |
| --- | --- | --- |
| stop `arrival_seconds` | `planned_arrival_seconds` | accepted only when the preferred key is absent |
| stop `departure_seconds` | `planned_departure_seconds` | accepted only when the preferred key is absent |
| `incidents.json` | scenario `incidents` in `scenarios.json` | loaded into an implicit baseline only when `scenarios.json` is absent |

The old timetable names are compatibility aliases, not preferred examples or
model terminology.

## Validation and conversion

`validateSceneStructure` checks files, JSON shape, required sections, and field
types without cross-file reference checks. `validateScene` checks semantic
relationships and values. `validateRunnableScene` checks the minimum complete
model needed by a simulation. `validateSceneDirectory` loads and performs
semantic validation; `validateRunnableSceneDirectory` adds runnable-completeness
checks. Both stop after structural errors to avoid cascaded diagnostics.

`SceneImporter` reads legacy `trainNames.txt` entries or enumerated train
files. A legacy train definition has seven whitespace tokens in this order:

```text
operating_code entry_time_seconds headway_seconds route_index data_file traction_file timetable_file
```

It maps the explicit physical, traction, and timetable relationships into the
canonical files, imports numeric `Routes/Route<N>.txt` files as routes, reads
stations from the legacy track-line station file, and reports skipped or
unresolved values in `scene.json.import_report`. The importer writes the files
with the encoders of `SceneWriter`, so an imported scene has the form of a saved
one: `signalling_areas` is absent when the area sidecar lists no area. Import is
an explicit boundary; the resulting scene must be validated and corrected before
it is used when the report exposes ambiguous source relationships.

The reverse exporter remains available for interoperability. It does not make
legacy files authoritative, and the normal runtime never invokes it. Canonical
scenario selection and passengers execute directly from [`SceneModel`].

[`InitialParameters`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_initial_parameters.html
[`Train::computePaxDependentDwellTimeAtStations`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_train.html
[`SceneModel`]: https://ancientkingg.github.io/EGTRAIN/latest/api/struct_scene_model.html

## Scene directory

The application loads the six required JSON files, accepts optional scenarios,
passengers, display metadata in `views.json`, and historical
compatibility aliases, and validates before a run. Infrastructure, signalling,
rolling stock, services, the selected
scenario, and passengers are built directly from `SceneModel` in memory.

| File | Status | Contents |
| --- | --- | --- |
| `scene.json` | required | schema version, name, units, base time, simulation settings, import report |
| `infrastructure.json` | required | tracks, nodes, arcs, blocks, connections |
| `stations.json` | required | stations, positions, platforms, platform nodes |
| `signalling.json` | required | signals, routes, signalling areas, dependencies, restrictions, boundaries |
| `rolling_stock.json` | required | physical/traction rolling stock units and compositions |
| `services.json` | required | route/composition links and planned timetable stops |
| `scenarios.json` | optional on load; always written | default scenario, named scenarios, incidents, entrance delays |
| `passengers.json` | optional | journeys, absolute midnight-second windows, and legs |
| `views.json` | optional display metadata | authored track levels/regions and station display geometry; preserved in folders and bundles |

Signalling areas in `signalling.json` give runtime sections their signalling
level; see [Signalling levels](signalling-levels.md).

The writer emits preferred V1 keys. Stop plans use independently optional
`planned_arrival_seconds` and `planned_departure_seconds` on any stop, plus
required `dwell_seconds`. Legacy `-1` values remain omitted planned fields;
they are not treated as results or filled by a last-stop rule. See [Editing
examples](#editing-examples).

Scenario files use `default_scenario_id`, named `scenarios`, concrete
`incidents`, and `entrance_delays`. Passenger windows use absolute seconds
from midnight. The complete key contract and historical aliases are in the
sections above.

## Links between records

Edit canonical JSON or use the scene editor. Keep IDs unique and keep service
links consistent:

```text
service.composition -> rolling_stock.compositions[].id
service.route       -> signalling.routes[].id
stop.station        -> stations[].id
stop.platform       -> platform on that station
scenario incident   -> signal/block or service target as appropriate
```

## Versions

Application version, scene schema version, and bundle version are independent.
The saved-with application version is provenance and does not by itself prompt
for an upgrade. An upgrade to a copy is offered only for a scene version that
has a registered migration, and none exists today; when one is offered, the
original directory or ZIP remains unchanged. Newer schema or bundle versions
require a newer app. The compatibility boundary of the scene model in the
developer documentation lists which scenes open.

## Portable bundles

An `.egscene` file packages the canonical JSON files of a scene in a deterministic ZIP archive.
The bundle version describes the container; `schema_version` still describes
the canonical data. Bundles include `scenarios.json`, omit generated results
and legacy input, and do not replace editable directory scenes.

Use `scene_tool` to pack, inspect, or unpack a bundle:

```bash
./build/scene_tool pack path/to/scene case-study.egscene
./build/scene_tool validate case-study.egscene
./build/scene_tool unpack case-study.egscene path/to/unpacked-scene
```

See [Opening an `.egscene` case study](opening-a-case-study.md) for the student
workflow and [Scene bundle format](scene-bundle.md) for the
entry allowlist and size limits.

## Convert a legacy case

Use the GUI's `File > Load Legacy Case...` or `scene_tool import`. Select a
separate destination; the importer never modifies the source. A legacy train
definition is seven whitespace tokens:

```text
operating_code entry_time_seconds headway_seconds route_index data_file traction_file timetable_file
```

The importer preserves those explicit physical, traction, and timetable
relationships, retains the operating code separately from the unique canonical
service ID, maps numeric `Routes/Route<N>.txt` files to routes, and reads
stations from the track-line station file. It also preserves legacy
`GUI/caseStudyTrackData.txt` and `GUI/StationsCoord.txt` layout in `views.json`.
It reports missing references,
malformed rows, and preserved source anomalies in `scene.json.import_report`.
In particular, a timetable
sentinel of `-1` means the corresponding planned arrival or departure is
absent; the importer neither synthesizes a result nor changes an inconsistent
source time.

A `TrackLines/AreasCaseStudy.txt` is reported in `import_report` and in a
warning, but its rows are not converted: add the signalling areas in
Infrastructure > Signalling area.

Example:

```bash
./build/scene_tool import \
  /path/to/legacy-case \
  /tmp/netherlands-v1 \
  Netherlands
./build/scene_tool validate /tmp/netherlands-v1
```

Inspect importer diagnostics before opening the result. Structural/import
errors prevent a usable scene from being published; semantic diagnostics stay
with the scene for repair.

## Editing examples

Use these examples when editing a scene by hand. Keys are case-sensitive and the
preferred spellings are the ones shown here. The complete type and validation
reference is in the sections above.

### `scene.json`

```json
{
  "schema_version": 1,
  "name": "Example",
  "base_time": "08:00:00",
  "units": {"distance": "m", "time": "s", "speed": "m/s"},
  "simulation_settings": {
    "duration_seconds": 3600,
    "buffer_time_seconds": 0,
    "recovery_time_percent": 0
  }
}
```

`name` and `schema_version` are required. `description`, `base_time`,
`units`, and `simulation_settings` are optional. An `import_report` array
records conversion rows with `category`, optional `source_file`,
`source_count`, `converted_count`, `skipped_count`, and
`unresolved_references`.

The four simulation settings above are the scene-level timing controls.
Legacy counts, GUI/output/network flags, and output paths do not belong here.

### Infrastructure and stations

`infrastructure.json` uses these preferred arrays:

```json
{
  "tracks": [{"id": "track.1"}],
  "nodes": [{"id": "node.1", "track": "track.1", "x_km": 0, "y_km": 0}],
  "arcs": [{"id": "arc.1", "track": "track.1", "from": "node.1", "to": "node.2", "curvature_radius_m": 0, "gradient_percent": 0, "speed_limit_ms": 30}],
  "blocks": [{"id": "block.1", "track": "track.1", "length_km": 1}],
  "connections": [{"id": "connection.1", "from": "node.2", "to": "node.3", "speed_limit_ms": 10}]
}
```

`nodes` and `arcs` are required arrays. `tracks`, `blocks`, and `connections`
may be empty or omitted on input. A station has `id`, `name`, optional
`position_km`, and optional `platforms`; each platform has `id` and a `nodes`
array of node IDs. Optional positive finite `length_m` and `width_m` values
control passenger capacity. When either key is absent, its effective value is
100 m or 2.5 m respectively, preserving existing scenes.

Block IDs cannot contain `/`. The section catalog reserves that character for
connection-derived section identities and reports existing conflicting IDs as
validation errors without preventing the scene from loading.

### Signalling

`signalling.json` requires `signals` and `routes` arrays. A signal has `id`
and may have `protected_section`, an exact section-catalog reference used to
resolve signal-failure incidents. Empty bindings are omitted by the writer;
an unbound signal is actionable validation output when used by an incident.
A direct base-block incident target remains compatible; a target matching both
a signal and section is rejected as ambiguous. A route has `id`, a
string-array `blocks`, and optional `corridor` and `reversed`; its block tokens
are authored in forward or reverse order and are never silently sorted.
Validation reports `scene.route.direction` for a route marked `reversed` whose
sections run forward.
Continuity is required inside one connected region. A scene with legacy-import
provenance retains historical cross-region coordinate jumps with a warning;
newly authored scenes require a declared connection. Compound tokens must be
exact derived section IDs.

Use these optional arrays for explicit signalling relationships:

- `signalling_areas`: `id`, numeric `start_km`, numeric `end_km`, integer
  `level` from 0 through 5, and optional canonical `track`. A section must fit
  completely inside the area; a section that an area edge cuts is not covered.
  Track-scoped areas override network-wide areas; missing coverage remains unset,
  and Run review warns when a route section has no level. See
  [Signalling levels](signalling-levels.md) for what each level
  does.
- `block_dependencies`: `{ "block": "...", "depends_on": "..." }`.
- `single_track_restrictions`: `start_block`, `end_block`,
  `protected_start_block`, and `protected_end_block`. While a train is in the
  stretch, trains of the opposite direction wait in front of it. This needs a
  signalling level; see
  [Single-track restrictions](#single-track-restrictions).
- `station_boundaries`: `entrance_block`, optional `exit_block`, and optional
  boolean `direction`.

Signals, dependencies, virtual signals, and track-detection sections used by
the current simulator are not interchangeable: derived runtime topology is
documented in the [migration matrix](v1-scene-migration-matrix.md).

### Rolling stock

Each `train_units[]` item has an `id` and may have `physical`,
`traction_curve`, and `source`. `physical` uses the exact nine keys:

```text
mass_of_traction_unit_kg, mass_of_a_wagon_kg, number_of_wagons,
max_speed_ms, max_deceleration_ms2, frontal_area_m2,
resistance_coefficient, jerk_ms3, length_m
```

Each traction row is `[v_lower, v_upper, c0, c1, c2]`. `source` contains the
independent optional paths `data_file` and `traction_file`; do not infer a
traction path from `LITRA`, `T_`, or any other filename convention.

The rolling stock editor refuses a number outside the hard limit of its field.
Validation warns about a number outside the warning bound
(`scene.train.value.implausible`, one warning for each number; it does not block
Run). A file that holds a number beyond a hard limit keeps it: the editor shows
the nearest limit and leaves the stored number as it is until you type another
one. The limits guard against nonsense input and are not data of a vehicle.

| Field | Unit | Hard limit | Warning bound |
| --- | --- | --- | --- |
| `mass_of_traction_unit_kg` | kg | 0 to 5e6 | 0 to 5e5 |
| `mass_of_a_wagon_kg` | kg | 0 to 1e6 | 0 to 2e5 |
| `number_of_wagons` | wagons | 0 to 1000 | 0 to 200 |
| `max_speed_ms` | m/s | 0 to 200 | 0 to 120 |
| `max_deceleration_ms2` | m/s² | 0 to 10 | 0 to 3 |
| `frontal_area_m2` | m² | 0 to 1000 | 0 to 50 |
| `resistance_coefficient` | per km/h | 0 to 1 | 0 to 0.1 |
| `jerk_ms3` | m/s³ | 0 to 100 | 0 to 10 |
| `length_m` | m | 0 to 3000 | 0 to 1500 |
| `v_lower`, `v_upper` | m/s | 0 to 200 | 0 to 120 |
| `c0` | N | -1e9 to 1e9 | -5e6 to 5e6 |
| `c1` | N s/m | -1e9 to 1e9 | -1e6 to 1e6 |
| `c2` | N s²/m² | -1e9 to 1e9 | -5e6 to 5e6 |

The editor and the validator read the same table, in `scene/SceneFields.h`.

Each `compositions[]` item has an `id` and a non-empty `units` array of train
unit IDs. Services refer to the composition ID, not directly to a source file.

### Services and planned timetable

The root of `services.json` is `services`. A service requires `id`,
`composition`, `route`, and `stops`. Optional service properties are
`operating_code`, `performance_percent`, `maximum_speed_kmh`, `through`,
`entry_time_seconds`, and `repeat`. `id` is the unique canonical reference;
each occurrence is identified by `(service id, occurrence)` with occurrence
numbered from 1. `operating_code` defaults to `id` and may be shared by
distinct services. The existing runtime train key remains `service-id-occurrence`.

`performance_percent` is a finite percentage from `1` through `100`, default
`100.0`. It scales tractive effort and the commanded maximum speed exactly
once; it does not scale braking, mass, shared composition data, buffer, or
recovery. `maximum_speed_kmh`, when present, is a positive finite cap in km/h.
The native speed precedence is:

```text
min(composition maximum speed, maximum_speed_kmh if present)
    * performance_percent / 100
```

The 100% case uses the unchanged raw force/speed path. A repeat requires a
positive finite `headway_seconds` in seconds. Optional `repeat.count` is a
positive integer total occurrence count, including the base; it overrides
`ceil(duration_seconds / headway_seconds)`. If omitted, that ceiling remains
the horizon. Entry and stop times are offset by
`(occurrence - 1) * headway_seconds`.

Optional `repeat.operating_code_step` must be nonzero and requires a decimal
base operating code. A base `1723` with step `2` therefore exposes `1723`,
`1725`, `1727`; without a step, a repeat exposes a readable base-plus-
occurrence code. The canonical service ID never advances. A non-empty
`SceneRunSelection` builds only the listed service/occurrence identities and
skips excluded delays and any passenger journey that is not fully covered;
an empty selection builds all.
Selections must refer to occurrences inside the service horizon.

The legacy importer retains the first token of each `Trains` definition here
instead of changing it when duplicate canonical IDs need a suffix. A legacy
train breakdown targeting a shared code is expanded to each matching service
because the current runtime applies that prefix to every matching train; import
reports the expansion.

Each stop requires `station` and `dwell_seconds`; `platform` is optional. The
preferred planned-time keys are independently optional on every stop:

```json
{
  "station": "station.1",
  "planned_arrival_seconds": 120,
  "planned_departure_seconds": 180,
  "dwell_seconds": 60
}
```

The values are planned seconds relative to the scene base-time origin. A stop
may omit arrival, departure, either one, or both. Legacy timetable `-1`
values are imported as absent fields; the importer does not synthesize or
raise a value. Validation may warn about an incomplete intermediate schedule.
These fields are input plans, never simulation results. A service with no
stops may be marked `through: true`; a repeated service uses a positive
`repeat.headway_seconds`.

### Scenarios

`scenarios.json` uses the preferred root keys `default_scenario_id` and
`scenarios`:

```json
{
  "default_scenario_id": "baseline",
  "scenarios": [
    {
      "id": "baseline",
      "name": "Baseline",
      "incidents": [],
      "entrance_delays": []
    },
    {
      "id": "failure-1",
      "name": "Signal failure",
      "incidents": [{"id": "inc.1", "type": "signal_failure", "target": "signal.1", "start_seconds": 300, "end_seconds": 900}],
      "entrance_delays": [{"service": "service.1", "occurrence": 1, "station": "station.1", "delay_seconds": 120}]
    }
  ]
}
```

An incident is concrete input with `id`, `type`, `target`, and finite
non-negative `start_seconds`. Valid types are `signal_failure` and
`train_breakdown`. Signal failures require `end_seconds`; the interval is
start-inclusive and end-inclusive for compatibility. Legacy full-hold
breakdowns also require an end, with `[start_seconds, end_seconds)` semantics.
A reduced-speed breakdown may omit `end_seconds`, in which case its cap lasts
until the targeted occurrence reaches its normal route end. When present,
`end_seconds` is recovery time.

Breakdown-only optional fields are:

- `occurrence`: positive 1-based occurrence of the target service. Omit it to
  target every occurrence; a configured occurrence must be within the service
  repeat range.
- `reduced_speed_kmh`: positive finite cap on the matched occurrence's
  effective trajectory speed. It does not mutate composition or service
  maximum-speed data.
- `terminate_at_destination`: retain a destination-termination request after
  recovery and report whether the occurrence reached its normal route end.

Signal failures reject these breakdown-only fields. The four-column legacy
`Incidents.txt` export skips enhanced breakdowns and emits a compatibility
diagnostic because it cannot encode occurrence, cap, recovery omission, or
termination semantics.

If no scenario file is supplied, the loader creates a baseline; the historical
flat incident file is read into that baseline.

The current results UI can freeze an incident-free, no-entrance-delay run with
**Set delay baseline**. A comparison run may have incidents, entrance delays,
or both. The delay analysis window reports the difference from the baseline and
the recorded direct incident evidence, and never states a cause. The matching
rules and the figures are in
[How delays are calculated](delay-analysis.md#delay-comparison-against-a-baseline).

### Passengers

`passengers.json` is optional. Its root is `passengers`; each passenger has
`id` and `journeys`. A journey uses `id`, `origin`, `destination`, optional
`activity`, required `planned_departure` and `planned_arrival` windows, and
`legs`. Each window contains `start_seconds` and `end_seconds` as absolute
seconds from midnight. Each leg has `id`, `origin`, `destination`, `service`,
and optional `occurrence` (default `1`).

For each leg, the destination must occur after the origin in the referenced
service's ordered stop pattern. Repeated stations are resolved as the first
ordered pair that satisfies that rule. Legacy-import scenes retain an invalid
historical leg as a warning, but native staging omits that whole journey.

Journey and leg IDs are scene-wide stable IDs. An empty `legs` array preserves
a DAS journey for which the legacy route-choice input has no matching row; the
validator warns instead of guessing a route.

Passenger JSON is native runtime input. The explicit legacy importer recognizes
DAS and RouteChoice only when both exact files exist:
`Passengers/DAS_FrenchCaseStudy.csv` and
`Passengers/RouteChoiceFC_EQ1.csv`. After conversion the runtime reads the
canonical journeys and legs; random draws and generated passenger results are
not scene input. The Passengers dock supports passenger, journey, and ordered-leg
editing. Its Import action appends non-colliding passenger IDs from that same
file pair and keeps unresolved references visible for correction or deletion.

### Compatibility aliases

The loader accepts these historical forms when the preferred key is absent;
`SceneWriter` emits the preferred form:

| Historical form | Preferred form |
| --- | --- |
| stop `arrival_seconds` | `planned_arrival_seconds` |
| stop `departure_seconds` | `planned_departure_seconds` |
| optional `incidents.json` | scenario incidents in `scenarios.json` |

### Save, load, and run

`Save Scene` writes the canonical files and removes stale flat incident data.
`loaded_data` is runtime metadata, not an editable source section. `views.json`
is display-only.

The editor and simulator use canonical data directly. `legacy/` is not a V1
scene requirement; import and export are explicit interoperability actions.

Use the three validation layers before running:

1. structural shape (`validateSceneStructure`),
2. semantic references and values (`validateScene`),
3. runnable completeness (`validateRunnableScene`).

See the sections above for the exact file
contract and [V1 migration matrix](v1-scene-migration-matrix.md) for the
legacy consumers and derived/output families.
