# V1 scene property reference

Use this reference while editing a V1 scene by hand. The application guide explains the full workflow: [Using EGTRAIN and authoring V1 scenes](scenes-and-application.md).

Each scene is a directory. JSON object and property names are case sensitive. A property marked "required" must exist and have the stated type. An optional property uses the default listed below when it is absent.

This reference describes the implementation as it works now. V1 is not yet a self-contained simulation format. Some JSON sections are canonical and used by the loader, validator, and exporter; other sections are placeholders or parse-only files. A runnable imported scene still needs files under `legacy/`.

## `scene.json`

| Property | Type | Required | Meaning | Accepted value and checks |
| --- | --- | --- | --- | --- |
| `schema_version` | integer | yes | Version of the canonical scene data | Must be `1`. Other values stop loading. |
| `name` | string | yes | Scene name shown in the application | Must not be empty. |
| `description` | string | no | Short note about the case | Defaults to an empty string. |
| `base_time` | string | no | Clock time used as the zero point for service times | Must match `HH:MM:SS`, for example `08:00:00`. Defaults to an empty value. The validator checks the shape, not the hour, minute, or second ranges. |
| `units` | object | no | Unit declarations for scene values | Defaults to metres, seconds, and metres per second. |
| `units.distance` | string | no | Distance unit | If present, it must be `m`. |
| `units.time` | string | no | Time unit | If present, it must be `s`. |
| `units.speed` | string | no | Speed unit | If present, it must be `m/s`. |
| `legacy_source` | string | no | Path recorded by the legacy importer | Kept as provenance only. The V1 loader ignores it and `Save Scene` does not write it back. |

All times in the other files are measured in seconds relative to `base_time`. Negative service times are allowed because a timetable can start before the base time.

## `infrastructure.json`

| Property | Type | Required | Meaning | Accepted value and checks |
| --- | --- | --- | --- | --- |
| `nodes` | array | yes | Reserved canonical infrastructure nodes | The loader checks that this is an array but does not read its entries in V1. Use `[]`. |
| `arcs` | array | yes | Reserved canonical infrastructure links | The loader checks that this is an array but does not read its entries in V1. Use `[]`. |

The current simulator reads track geometry, blocks, switches, gradients, and speed limits from the exported `TrackLines/` directory. The exporter accepts `legacy/Tracklines/` or `legacy/TrackLines/` and normalizes the name. Editing `nodes` or `arcs` has no effect on a run. `Save Scene` rewrites both arrays as `[]`, so it does not preserve entries placed in them.

## `stations.json`

The root property is `stations`, a required array.

| Property | Type | Required | Meaning | Accepted value and checks |
| --- | --- | --- | --- | --- |
| `stations[].id` | string | yes | Identifier used by service stops | Must not be empty and must be unique among stations. Keep it free of whitespace so the scene can export to the legacy timetable format. |
| `stations[].name` | string | yes | Name shown to the user | Must not be empty. It does not act as a reference. |
| `stations[].position_km` | number | no | Track position emitted by the legacy importer | The V1 loader ignores it and `Save Scene` does not write it back. The simulator obtains the station position from `legacy/TrackLines/`. |
| `stations[].platforms` | array | no | Platforms that a stop may name | Defaults to an empty array. A value of another type is currently treated as an empty array. |
| `stations[].platforms[].id` | string | yes, when the platform exists | Platform identifier within the station | Must not be empty. A service platform must match one of the IDs on its station. The validator does not check duplicate platform IDs. |

## `signalling.json`

The root properties `signals` and `routes` are required arrays.

| Property | Type | Required | Meaning | Accepted value and checks |
| --- | --- | --- | --- | --- |
| `signals[].id` | string | yes | Signal identifier | Must not be empty and must be unique among signals. A `signal_failure` incident uses this ID as its target. |
| `routes[].id` | string | yes | Route identifier used by services | Must not be empty and must be unique among routes. Imported legacy routes use IDs such as `route29`. |
| `routes[].blocks` | array of strings | yes | Ordered block tokens followed by the route | Must contain at least one string. Non-string entries are ignored. The validator checks for an empty result, but it does not resolve block IDs against the legacy track files. |

Route direction and suitability are properties of the ordered block list, not the route number. Run a train over a route before assigning it to a Sprinter or Intercity service.

## `rolling_stock.json`

The root properties `train_units` and `compositions` are required arrays. At least one unit and one composition must exist.

### Train units

| Property | Type | Required | Meaning | Accepted value and checks |
| --- | --- | --- | --- | --- |
| `train_units[].id` | string | yes | Train unit identifier | Must not be empty and must be unique among train units. |
| `train_units[].physical` | object | no for loading | Physical values needed to export and run the unit | If present, all nine properties below are required numbers. Export fails when a service uses a unit without physical data. |
| `physical.mass_of_traction_unit_kg` | number | with `physical` | Mass of the powered unit in kilograms | The loader does not impose a range. |
| `physical.mass_of_a_wagon_kg` | number | with `physical` | Mass of one wagon in kilograms | The loader does not impose a range. |
| `physical.number_of_wagons` | number | with `physical` | Number of wagons attached to the powered unit | Stored as a number for legacy compatibility. The loader does not require an integer or impose a range. |
| `physical.max_speed_ms` | number | with `physical` | Maximum train speed in metres per second | The loader does not impose a range. Convert km/h by dividing by `3.6`. |
| `physical.max_deceleration_ms2` | number | with `physical` | Maximum service deceleration in metres per second squared | The loader does not impose a range. |
| `physical.frontal_area_m2` | number | with `physical` | Frontal area used by the resistance model, in square metres | The loader does not impose a range. |
| `physical.resistance_coefficient` | number | with `physical` | Coefficient used by the train resistance calculation | The loader does not impose a range. Copy it from verified train data. |
| `physical.jerk_ms3` | number | with `physical` | Maximum change in acceleration per second | Measured in metres per second cubed. The loader does not impose a range. |
| `physical.length_m` | number | with `physical` | Total unit length in metres | The loader does not impose a range. |
| `train_units[].traction_curve` | array | no for loading | Piecewise traction-force curve | Each row must contain exactly five numbers. Export fails when a used unit has no rows. |
| `traction_curve[][0]` | number | in each row | Lower speed bound, in metres per second | The loader does not check ordering or overlap. |
| `traction_curve[][1]` | number | in each row | Upper speed bound, in metres per second | The simulator applies a row when speed is within its interval. |
| `traction_curve[][2]` | number | in each row | Constant coefficient `C0` | Traction force is `C0 + C1 * v + C2 * v^2` within the row's speed interval. |
| `traction_curve[][3]` | number | in each row | Linear coefficient `C1` | Used in the traction-force formula above. |
| `traction_curve[][4]` | number | in each row | Quadratic coefficient `C2` | Used in the traction-force formula above. |
| `train_units[].source` | object | no | Original legacy train-data paths | If present, both properties below must be non-empty strings. The exporter uses their base filenames. |
| `source.data_file` | string | with `source` | Source file for the nine physical values | Usually a path such as `/TrainData/SLT06.txt`. |
| `source.traction_file` | string | with `source` | Source file for the traction curve | Usually a path such as `/TrainData/T_SLT06.txt`. |

### Compositions

| Property | Type | Required | Meaning | Accepted value and checks |
| --- | --- | --- | --- | --- |
| `compositions[].id` | string | yes | Identifier used by services | Must not be empty and must be unique among compositions. |
| `compositions[].units` | array of strings | yes | Ordered train units in the consist | Must contain at least one known train-unit ID. Non-string entries are ignored. Repeating an ID represents coupled copies of that unit. |

When a composition contains several units, the exporter combines their masses, limits, lengths, resistance values, and traction curves into one legacy train definition.

## `services.json`

The root property is `services`, a required array. At least one service must exist.

### Service properties

| Property | Type | Required | Meaning | Accepted value and checks |
| --- | --- | --- | --- | --- |
| `services[].id` | string | yes | Service identifier | Must not be empty and must be unique among services. A `train_breakdown` incident uses this ID as its target. |
| `services[].composition` | string | yes | Train consist used by the service | Must match a composition ID in `rolling_stock.json`. |
| `services[].route` | string | yes | Infrastructure route used by the service | Must match a route ID in `signalling.json`. |
| `services[].through` | boolean | no | Marks a service that crosses the scene without scheduled stops | Defaults to `false`. A through service with stops produces a warning. |
| `services[].entry_time_seconds` | number | no | Time when the train enters the simulation | Defaults to the first stop's departure during legacy export, or `0` if that departure is absent. |
| `services[].repeat` | object | no | Repetition settings | When absent, the service is exported as a one-shot service. |
| `services[].repeat.headway_seconds` | number | with `repeat` | Time between repeated trains | Must be greater than `0`. |
| `services[].stops` | array | yes | Ordered timetable stops | May be empty. An empty non-through service produces a warning. |

### Stop properties

| Property | Type | Required | Meaning | Accepted value and checks |
| --- | --- | --- | --- | --- |
| `stops[].station` | string | yes | Station where the train stops | Must match a station ID in `stations.json`. |
| `stops[].platform` | string | no | Platform used at the station | If present and non-empty, it must match a platform ID on that station. |
| `stops[].arrival_seconds` | number | no | Planned arrival relative to `base_time` | May be omitted on any stop. It is normally absent at the first stop. Negative values are allowed. |
| `stops[].departure_seconds` | number | yes, except at the last stop | Planned departure relative to `base_time` | Must be at or after the arrival at the same stop. A lower value than the previous stop's departure produces a warning. Negative values are allowed. |
| `stops[].dwell_seconds` | number | yes | Planned stationary time at the stop | Must be `0` or greater. A value larger than departure minus arrival produces a warning. |

Keep stops in travel order. Verify that each station belongs to the chosen route because the validator cannot infer station order from legacy block tokens.

## `incidents.json`

This file is optional. When present, its root property `incidents` is a required array.

| Property | Type | Required | Meaning | Accepted value and checks |
| --- | --- | --- | --- | --- |
| `incidents[].id` | string | yes | Incident identifier | Must not be empty and must be unique among incidents. |
| `incidents[].type` | string | yes | Kind of disruption | Must be `signal_failure` or `train_breakdown`. |
| `incidents[].target` | string | yes | Object affected by the incident | Must match a signal ID for `signal_failure` or a service ID for `train_breakdown`. |
| `incidents[].start_seconds` | number | yes | Start time relative to `base_time` | Must be `0` or greater. |
| `incidents[].end_seconds` | number | yes | End time relative to `base_time` | Must be greater than `start_seconds` and `0` or greater. |

`Save Scene` removes `incidents.json` when the scene has no incidents.

## `views.json`

This optional file stores display settings. The V1 loader only checks that it contains valid JSON with an object at the root. It does not interpret or validate any property. `Save Scene As...` copies the file, while `Save Scene` leaves it unchanged.

Do not depend on a `views.json` property for simulation behavior.

## `legacy/`

`legacy/` is a directory, not a JSON property. It contains data that the V1 model does not yet represent. Imported scenes need it for a simulation run.

Common contents include:

| Path | Purpose |
| --- | --- |
| `Tracklines/` or `TrackLines/` | Track geometry, block layout, stations, switches, gradients, and speed limits used by the simulator |
| `TMS/` and `TDS/` | Legacy signalling and train-detection data |
| `GUI/` | Legacy display coordinates and related settings |
| `Rescheduling/`, `Passengers/`, and `RoutesToWrite/` | Optional case-specific runtime data not represented by the V1 model |
| `Timetable/Scenarios_*` | Scenario directories copied through without conversion |
| Non-route files under `Routes/` | Route support files copied through without conversion |

The exporter writes routes, trains, timetables, and incidents from the canonical JSON first. It then copies files from `legacy/` only when the generated output does not already contain the destination. Do not edit generated files in an export and expect the changes to return to the V1 scene.

### Work that removes the legacy dependency

Issue status checked on 13 July 2026:

- [#83](https://github.com/Ancientkingg/EGTRAIN/issues/83) is the open epic for making the scene model the simulator's native input.
- [#84](https://github.com/Ancientkingg/EGTRAIN/issues/84) converted the four case studies, but it is complete and did not remove their `legacy/` passthrough.
- [#85](https://github.com/Ancientkingg/EGTRAIN/issues/85) covers the main format gap: building track, block, switch, station, connection, route, and signalling structures from canonical scene data.
- [#86](https://github.com/Ancientkingg/EGTRAIN/issues/86) replaces the legacy rolling stock, service, and timetable loaders.
- [#87](https://github.com/Ancientkingg/EGTRAIN/issues/87) loads incidents directly from the scene model.
- [#88](https://github.com/Ancientkingg/EGTRAIN/issues/88) changes the GUI and CLI run path so it no longer stages a legacy export before simulation.
- [#89](https://github.com/Ancientkingg/EGTRAIN/issues/89) removes the old text loaders and input trees after the native path works.

These issues cover the main simulation path, but the tracking is incomplete. No dedicated issue currently converts `GUI/`, `TMS/`, `TDS/`, `Rescheduling/`, `Passengers/`, or `RoutesToWrite/` into canonical scene properties. [#125](https://github.com/Ancientkingg/EGTRAIN/issues/125) makes missing or unimplemented data visible in the UI, but it does not define or load those properties.

Issue #85 also needs a schema and importer step. Its current text says to build infrastructure from `SceneModel`, but the current model has no node or arc fields, the loader only checks that the two arrays exist, and the importer writes both arrays empty.

The bundle work in [#99](https://github.com/Ancientkingg/EGTRAIN/issues/99), [#100](https://github.com/Ancientkingg/EGTRAIN/issues/100), and [#102](https://github.com/Ancientkingg/EGTRAIN/issues/102) says that `.egscene` files exclude `legacy/`. That is not sufficient for the current run path. A portable bundle either needs the native-input work above and coverage for the remaining passthrough data, or it must carry `legacy/` until that migration is complete.

## Validation command

Run the validator after editing any file:

```bash
./build/scene_tool validate path/to/scene
```

An error blocks the scene from running. A warning points to a timetable or modelling issue that needs inspection but does not block the run. The full list of diagnostic codes is in the [scene schema reference](../architecture/scene-schema.md#diagnostics).
