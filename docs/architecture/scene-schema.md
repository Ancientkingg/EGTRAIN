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

For a V2 `.egscene` bundle, `scenarios.json` is required and `passengers.json`
is the only optional entry. `views.json`, `legacy/`, assets, results, and
unknown or nested ZIP entries are excluded; see
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
`InitialParameters` are not V1 scene keys.

## `infrastructure.json`

The preferred root keys are `tracks`, `nodes`, `arcs`, `blocks`, and
`connections`. `nodes` and `arcs` are required arrays; the other arrays may be
empty or omitted on input and are emitted by the writer.

- `tracks[]`: `{ "id": string }`.
- `nodes[]`: `id`, `track`, numeric `x_km`, numeric `y_km`.
- `arcs[]`: `id`, `track`, `from`, `to`, numeric `curvature_radius_m`,
  `gradient_percent`, and `speed_limit_ms`.
- `blocks[]`: `id`, `track`, numeric `length_km`.
- `connections[]`: `id`, `from`, `to`, and optional numeric `speed_limit_ms`.

## `stations.json`

The root key is `stations`. Each station has required string `id` and `name`,
optional numeric `position_km`, and optional `platforms`. A platform has an
`id` and an optional `nodes` array of node IDs.

## `signalling.json`

Required root arrays are `signals` and `routes`.

- `signals[]`: `{ "id": string }`.
- `routes[]`: required `id` and string-array `blocks`; optional string
  `corridor` and boolean `reversed`.
- `signalling_areas[]`: required `id`, numeric `start_km`, numeric `end_km`,
  and integer `level` from 0 through 5; optional `track` refers to a canonical
  track ID. The array is optional.
- `block_dependencies[]`: `{ "block": string, "depends_on": string }`.
- `single_track_restrictions[]`: preferred explicit
  `start_block`, `end_block`, `protected_start_block`, and
  `protected_end_block`.
- `station_boundaries[]`: required `entrance_block`, optional `exit_block`,
  and optional boolean `direction`.

A signalling area covers a native section only when the section's complete
coordinate span lies inside the area. Network-wide areas are applied before
track-scoped areas; a track-scoped value overrides the network-wide value.
Either connected track matches a derived switch section. Different values at
the same precedence tier that cover one section are invalid. The builder
applies areas after base and switch sections exist and before routes copy them.
A section with no matching area retains the unset signalling value. The loader
and writer never create a default level.

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

Optional runtime controls are:

- `performance_percent`: percentage, default `100.0`, finite range `1..100`.
  It scales available tractive effort and the commanded maximum speed, but not
  braking, mass, composition data, recovery, or buffer settings.
- `maximum_speed_kmh`: positive finite speed in km/h. It is a service cap and
  is applied as `min(composition max speed, service cap)` before performance
  scaling. The writer omits it when it is not configured.
- `through`: boolean, and `entry_time_seconds`: seconds relative to the scene
  base-time origin.
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
- `dwell_seconds`: required numeric planned dwell, normally non-negative.

Arrival and departure are independently optional on every stop. There is no
last-stop-only departure rule. Legacy timetable `-1` values are preserved as
missing planned fields; the importer does not synthesize a value from the
other field. Validation may warn when an intermediate schedule is incomplete.
These are input plans in seconds relative to the scene's base-time origin, not
simulation results.

An empty `stops` array is valid for a through service; `through: true` records
that intent explicitly.

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
the delay baseline. A comparison run must contain incidents, no entrance
delays, and the same scene revision, base time, duration, timestep, and full
selected `(service_id, occurrence)` identity set; its scenario ID must differ
from the baseline. Baseline and scenario results are value snapshots, not
runtime train pointers.

For each occurrence, final arrival is the simulated arrival at its last
authored timetable station and call. Baseline and scenario must use the same
endpoint and both arrivals must be available; otherwise comparison is rejected.
Only positive `scenario - baseline` values are rows, and their exact sum is
the total arrival delay. A positive row is `primary` only when the scenario
occurrence has direct runtime evidence (incident ID plus first direct time and
location); otherwise it is `secondary`. No attribution is inferred from a
timetable difference alone. A comparison is rejected when the incident run
has no direct evidence anywhere. Signal-failure evidence is direct only when
the train stops at an active `SignalFailure` EoA; a breakdown hold or applied
speed cap is direct evidence. Destination request and termination outcome are
reported with each row.

## `passengers.json`

This file is optional. Its root key is `passengers`. Each passenger has `id`
and `journeys`. A journey has `id`, `origin`, `destination`, optional
`activity`, required `planned_departure` and `planned_arrival` objects, and a
`legs` array. Each time-window object uses numeric `start_seconds` and
`end_seconds`, expressed as absolute seconds from midnight, not seconds from
`base_time`. Each leg has `id`, `origin`, `destination`, `service`, and an
optional integer `occurrence` that defaults to `1`.

Journey and leg IDs are stable across the scene. A journey may have an empty
`legs` array when the active legacy route-choice file has no row for it; this
is reported as a warning rather than inventing a route.

Random draws and simulation results are not scene input. The explicit legacy
importer recognizes passenger data only when both exact source files
`Passengers/DAS_FrenchCaseStudy.csv` and
`Passengers/RouteChoiceFC_EQ1.csv` exist. Once converted, the native runtime
uses `passengers.json` and does not reopen those CSVs.

## `views.json` and runtime metadata

`views.json` is optional JSON for display defaults. It is not interpreted as
simulation input. `loaded_data` and `sourceFiles` are derived in-memory
metadata; they are recomputed when a scene is loaded and are not the source of
canonical values.

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
unresolved values in `scene.json.import_report`. Import is an explicit boundary;
the resulting scene must be validated and corrected before it is used when the
report exposes ambiguous source relationships.

The reverse exporter remains available for interoperability. It does not make
legacy files authoritative, and the normal runtime never invokes it. Canonical
scenario selection and passengers execute directly from `SceneModel`.
