# V1 Scene Model

V1 is a directory of canonical JSON files. The model is the hand-off between
the scene editor and the existing simulator path:

```text
legacy case -> SceneImporter -> canonical SceneModel + legacy passthrough
canonical files -> loadScene -> validation -> editor
canonical model -> native infrastructure -> native operations globals
canonical model -> SceneExporter -> legacy input -> existing simulator
```

Milestone 3 adds a filesystem-free operations builder after the native
infrastructure builder. It consumes canonical services, compositions,
scenarios, entrance delays, and passengers directly into the existing runtime
globals. The legacy exporter/default run path remains in place until the M4
cutover; the native builder is not called by normal GUI or CLI startup yet.

## Canonical directory

| File | Role | Input status |
| --- | --- | --- |
| `scene.json` | schema version, identity, units, base time, simulation settings, and optional import report | required |
| `infrastructure.json` | tracks, nodes, arcs, blocks, and connections | required |
| `stations.json` | stations, positions, platforms, and platform nodes | required |
| `signalling.json` | signals, routes, dependencies, single-track restrictions, and station boundaries | required |
| `rolling_stock.json` | train units, physical data, traction curves, and compositions | required |
| `services.json` | services, explicit route/composition links, stops, planned times, dwell, and repetition | required |
| `scenarios.json` | default scenario, named scenarios, incidents, and entrance delays | optional on load; always written by `SceneWriter` |
| `passengers.json` | passenger journeys and legs | optional |
| `views.json` | display preferences | optional |

The six required files are the files needed for structural loading. A scene
may therefore load without scenarios or passengers; the writer creates a
baseline `scenarios.json` even when there are no named scenarios. Imported
scenes also carry a `legacy/` tree for files still consumed by the legacy
runtime.

## Model ownership

`SceneModel` owns the canonical values, not a second flat copy of legacy
incidents. Incidents belong to a `SceneScenario`; the selected scenario is
identified by `default_scenario_id`. Runtime `loadedData` and `sourceFiles` are
derived summaries. `import_report` is the persisted conversion summary, with
one row containing `category`, optional `source_file`, `source_count`,
`converted_count`, `skipped_count`, and `unresolved_references`.

The scene-level simulation settings are deliberately small: `base_time`,
`simulation_settings.duration_seconds`,
`simulation_settings.buffer_time_seconds`, and
`simulation_settings.recovery_time_percent`. Legacy case counts, GUI flags,
output paths, and network feature flags are not scene-model settings.

Services keep physical and traction provenance explicit. A train unit's
`source.data_file` and `source.traction_file` are independent relationships;
the model does not infer one from a filename such as `LITRA` or `T_...`.

Service stop input uses `planned_arrival_seconds` and
`planned_departure_seconds`. Each is independently optional on every stop.
Legacy timetable sentinels such as `-1` become absent planned fields; they
are not converted into simulation results. An incomplete intermediate
schedule may produce a validation warning, but departure omission is not
restricted to the last stop. Dwell and repetition remain planned input as
well.

Each service also has a unique canonical `id` and an optional
`operating_code`. The latter defaults to the ID and preserves the active train
identity consumed by the existing simulator. It is intentionally not required
to be unique: Milano-Brescia contains distinct service definitions sharing
codes `9707` and `9709`.

Passenger journey windows use absolute seconds from midnight. They are not
random passenger draws, simulation results, or a replacement for the legacy
runtime's conditional DAS/RouteChoice CSV loader.

The native operations path gives every expanded occurrence the stable runtime
identity `<service id>-<occurrence>` and uses the canonical service ID for
breakdown targets and passenger legs. Repetition keeps the canonical entry in
`scheduled_departure_time`; the compatibility hourly-retiming algorithm may
derive `departure_time` while leaving that canonical value unchanged. Arrival
and departure timetable values remain independently optional and are staged as
runtime `-1` when absent, including repeated occurrences.

Only the selected scenario is applied: an explicit selection wins, otherwise
the exact default is used, with the first scenario used only when no default is
declared. Scenario entrance delays are resolved by service, occurrence, and
station; signal failures must resolve to exact runtime sections. Passenger
journeys and legs are built in memory, including journeys with no legs, and
their actual planned times are sampled from the canonical windows using the
existing random-number behavior. Platform stopping lists are populated from
the resolved occurrence stops without invoking the filesystem-era platform
loader.

## Validation layers

The public validator separates three questions:

- `validateSceneStructure` checks the directory, JSON, required sections, and
  field shape without cross-file semantic checks.
- `validateScene` checks the loaded model: references, identifiers, values,
  timetable consistency, incidents, and other scene relationships.
- `validateRunnableScene` checks the minimum complete model needed for a run.

`validateSceneDirectory` loads and performs semantic validation;
`validateRunnableSceneDirectory` adds runnable-completeness checks. Both run
model validation only when structural loading has no error diagnostics, which
avoids cascaded reference errors from partially parsed files.

## Compatibility and scope

The loader accepts historical aliases for existing scenes, while the writer
emits only preferred V1 keys. The aliases and their exact mappings are listed
in [Scene Schema Reference](scene-schema.md#historical-compatibility-aliases).

`SceneImporter` maps the legacy seven-token train definition, physical and
traction source files, timetable rows, routes, and stations into the model;
runtime support trees that are not yet native inputs are preserved under
`legacy/`. `SceneExporter` writes the canonical train, service, route, and
scenario-compatible data and carries the passthrough tree into the temporary
legacy input used by the current run path.

The compatibility exporter does not synthesize Rollout or passenger CSV files
from the new scenario/passenger JSON. Those values already round-trip through
the canonical loader, writer, importer, and validator, but their execution and
scenario selection remain later-milestone work; the unchanged runtime still
uses matching files from `legacy/` when present.

Milestone 1 covers this V1 directory and its compatibility path. The `.egscene`
bundle format and native simulator input are outside this milestone.

## Native infrastructure preview path

The native infrastructure/signalling builder accepts an already loaded and
validated `SceneModel` and populates the existing runtime infrastructure and
signalling globals without opening files or consulting `legacy/`. `TrackPreview`
also consumes the canonical model directly and resolves connections and station
markers through canonical node IDs. The normal simulation entry point still
uses the exporter and legacy runtime path. Native operations and selected
scenario mapping are now available as an explicit migration builder; full
native simulation cutover follows later.
