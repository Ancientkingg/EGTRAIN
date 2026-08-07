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

An imported scene may also contain `legacy/`. The current application exports
the canonical model to a temporary legacy input tree before starting the
existing simulator, so that passthrough remains part of the V1 contract.

## `scene.json`

Required keys:

- `schema_version`: integer `1`.
- `name`: non-empty string.

Optional keys:

- `description`: string.
- `base_time`: `HH:MM:SS` day-clock origin for the simulation.
- `units`: object containing only `distance: "m"`, `time: "s"`, and
  `speed: "m/s"` when present.
- `simulation_settings`: object with optional numeric
  `duration_seconds`, `buffer_time_seconds`, and
  `recovery_time_percent`.
- `import_report`: array of conversion rows.

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
- `block_dependencies[]`: `{ "block": string, "depends_on": string }`.
- `single_track_restrictions[]`: preferred explicit
  `start_block`, `end_block`, `protected_start_block`, and
  `protected_end_block`.
- `station_boundaries[]`: required `entrance_block`, optional `exit_block`,
  and optional boolean `direction`.

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
`route`, and `stops`; it may have boolean `through`, numeric
`entry_time_seconds`, string `operating_code`, and
`repeat: { "headway_seconds": number }`. The ID is the unique cross-file
reference. The operating code defaults to that ID, but may repeat: it preserves
the active legacy `Trains` token used for `Train::type`, train descriptions,
and the current compatibility export.

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
`train_breakdown`), `target`, `start_seconds`, and `end_seconds`. An entrance
delay has `service`, optional integer `occurrence`, `station`, and numeric
`delay_seconds`.

If `scenarios.json` is absent, loading creates a baseline scenario. A legacy
`incidents.json` is read into that baseline for compatibility. Saving writes
the scenario model to `scenarios.json` and removes the stale flat file.

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

Passenger JSON is distinct from the legacy passenger runtime. Random draws and
simulation results are not scene input. The legacy DAS/RouteChoice loader is
used only when both exact files
`Passengers/DAS_FrenchCaseStudy.csv` and
`Passengers/RouteChoiceFC_EQ1.csv` exist.

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
canonical files, imports numeric `Routes/Route<N>.txt` files as routes, and
reads stations from the legacy track-line station file. Selected runtime
support trees that still need passthrough are copied below `legacy/`. The
legacy runtime and export path remains active in Milestone 1.

That bridge does not generate passenger DAS/RouteChoice CSVs or entrance-delay
Rollout files from canonical JSON. In Milestone 1 those fields are authoritative
for persistence, import, and validation, while the unchanged simulator still
uses any corresponding files in `legacy/`. Native scenario selection and
canonical passenger execution belong to later milestones.
