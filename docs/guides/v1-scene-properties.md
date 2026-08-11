# V1 scene properties

Use this guide when editing a scene by hand. Keys are case-sensitive and the
preferred spellings are the ones shown here. The complete type and validation
reference is [V1 Scene Schema](../architecture/scene-schema.md).

## `scene.json`

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

## Infrastructure and stations

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
array of node IDs.

## Signalling

`signalling.json` requires `signals` and `routes` arrays. A signal has `id`
and may have `protected_section`, an exact section-catalog reference used to
resolve signal-failure incidents. Empty bindings are omitted by the writer;
an unbound signal is actionable validation output when used by an incident.
A direct base-block incident target remains compatible. A route has `id`, a
string-array `blocks`, and optional `corridor` and `reversed`; its block tokens
are authored in forward or reverse order and are never silently sorted.
Continuity is required inside one connected region. A scene with legacy-import
provenance retains historical cross-region coordinate jumps with a warning;
newly authored scenes require a declared connection. Compound tokens must be
exact derived section IDs.

Use these optional arrays for explicit signalling relationships:

- `signalling_areas`: `id`, numeric `start_km`, numeric `end_km`, integer
  `level` from 0 through 5, and optional canonical `track`. A section must fit
  completely inside the area. Track-scoped areas override network-wide areas;
  missing coverage remains unset.
- `block_dependencies`: `{ "block": "...", "depends_on": "..." }`.
- `single_track_restrictions`: `start_block`, `end_block`,
  `protected_start_block`, and `protected_end_block`.
- `station_boundaries`: `entrance_block`, optional `exit_block`, and optional
  boolean `direction`.

Signals, dependencies, virtual signals, and track-detection sections used by
the current simulator are not interchangeable: derived runtime topology is
documented in the [migration matrix](v1-scene-migration-matrix.md).

## Rolling stock

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

Each `compositions[]` item has an `id` and a non-empty `units` array of train
unit IDs. Services refer to the composition ID, not directly to a source file.

## Services and planned timetable

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
skips excluded delays and passenger legs; an empty selection builds all.
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

## Scenarios

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
**Set delay baseline**. An incident comparison must have incidents and no
entrance delays, different scenario IDs, and matching scene revision, base
time, duration, timestep, and selected `(service_id, occurrence)` identities.
Final arrival is the simulated arrival at the last authored timetable station
and call. The endpoint must match and be available in both runs. Only positive
scenario-minus-baseline contributions are included, and their exact sum is the
total delay. A positive row is `primary` only when that occurrence has direct
runtime incident evidence; otherwise it is `secondary`. Timetable differences
alone never establish causality, and comparison is rejected if the incident
run has no direct evidence anywhere. The table/CSV includes incident IDs,
first direct time/location, and destination termination outcome.

## Passengers

`passengers.json` is optional. Its root is `passengers`; each passenger has
`id` and `journeys`. A journey uses `id`, `origin`, `destination`, optional
`activity`, required `planned_departure` and `planned_arrival` windows, and
`legs`. Each window contains `start_seconds` and `end_seconds` as absolute
seconds from midnight. Each leg has `id`, `origin`, `destination`, `service`,
and optional `occurrence` (default `1`).

Journey and leg IDs are scene-wide stable IDs. An empty `legs` array preserves
a DAS journey for which the legacy route-choice input has no matching row; the
validator warns instead of guessing a route.

Passenger JSON is native runtime input. The explicit legacy importer recognizes
DAS and RouteChoice only when both exact files exist:
`Passengers/DAS_FrenchCaseStudy.csv` and
`Passengers/RouteChoiceFC_EQ1.csv`. After conversion the runtime reads the
canonical journeys and legs; random draws and generated passenger results are
not scene input.

## Compatibility aliases

The loader accepts these historical forms when the preferred key is absent;
`SceneWriter` emits the preferred form:

| Historical form | Preferred form |
| --- | --- |
| stop `arrival_seconds` | `planned_arrival_seconds` |
| stop `departure_seconds` | `planned_departure_seconds` |
| optional `incidents.json` | scenario incidents in `scenarios.json` |

## Save, load, and run

`Save Scene` writes the canonical files and removes stale flat incident data.
`loaded_data` is runtime metadata, not an editable source section. `views.json`
is display-only.

The editor and simulator use canonical data directly. `legacy/` is not a V1
scene requirement; import and export are explicit interoperability actions.

Use the three validation layers before running:

1. structural shape (`validateSceneStructure`),
2. semantic references and values (`validateScene`),
3. runnable completeness (`validateRunnableScene`).

See [V1 Scene Schema](../architecture/scene-schema.md) for the exact file
contract and [V1 migration matrix](v1-scene-migration-matrix.md) for the
legacy consumers and derived/output families.
