# V1 Scene Model

V1 is a directory of canonical JSON files. It is the only normal GUI, CLI,
preview, and simulation input:

```text
legacy case -> explicit SceneImporter -> canonical SceneModel
canonical files -> loadScene -> validation -> editor/preview
canonical model -> native infrastructure + operations -> simulator
canonical model -> explicit SceneExporter -> legacy interoperability files
```

`DispatchController::prepareScene` is the shared native setup seam. It consumes
the loaded model through the infrastructure/signalling and operations builders
and prepares runtime output directories. Neither builder opens provenance files
or consults an `Input/` tree.

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
| `views.json` | reserved display preferences; recognized but not consumed by the current model/runtime | optional and unsupported |

The six required files are the files needed for structural loading. A scene
may therefore load without scenarios or passengers; the writer creates a
baseline `scenarios.json` even when there are no named scenarios. Runnable
validation additionally requires populated infrastructure, routes, rolling
stock, services, and positive simulation duration.

## Model ownership

`SceneModel` owns the canonical values, not a second flat copy of legacy
incidents. Incidents belong to a `SceneScenario`; the selected scenario is
identified by `default_scenario_id`. Runtime `loadedData` and `sourceFiles` are
derived, non-serialized summaries. Loaded-data navigation targets point only to
existing editors and are not part of the canonical schema. `import_report` is
the persisted conversion summary, with
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
codes `9707` and `9709`. `performance_percent` defaults to `100.0` and must be
finite in `1..100`; `maximum_speed_kmh`, when present, is a positive finite
service cap in km/h. The native commanded speed is the composition maximum
limited by that cap, then multiplied by performance; 100% takes the legacy
raw path. Performance does not alter braking, mass, shared composition data,
buffer, or recovery.

Passenger journey windows use absolute seconds from midnight. They are not
random passenger draws or simulation results. DAS and RouteChoice CSV files
are read only by explicit legacy import and written only by explicit legacy
export.

The native operations path gives every expanded occurrence the stable identity
`(service id, 1-based occurrence)` and the existing runtime key
`<service id>-<occurrence>`. It uses the canonical service ID for breakdown
targets and passenger legs. A repeated service without a step exposes a
readable base-plus-occurrence operating code; a step is accepted only for a
decimal base (for example `1723`, `1725`, `1727`). An explicit repeat count is
the total occurrence count, including the base, and overrides the duration
horizon; without it, the count is `ceil(duration / headway)`. Entry and stop
offsets remain `(occurrence - 1) * headway`. The canonical service ID never
advances. Repetition keeps the canonical entry in `scheduled_departure_time`;
the compatibility hourly-retiming algorithm may derive `departure_time` while
leaving that canonical value unchanged. Arrival and departure timetable values
remain independently optional and are staged as runtime `-1` when absent,
including repeated occurrences.

Only the selected scenario is applied: an explicit selection wins, otherwise
the exact default is used, with the first scenario used only when no default is
declared. `DispatchController::prepareScene` and
`buildOperationsFromScene` accept a `SceneRunSelection`, a set of
service/occurrence identities. Empty means all; a non-empty selection is
validated, builds only selected trains, and skips delays and passenger legs for
excluded occurrences without modifying the scene. Capacity checks use the
selected train set. Scenario entrance delays are resolved by service,
occurrence, and station; signal failures must resolve to exact runtime
sections. Passenger journeys and legs are built in memory, including journeys
with no legs, and their actual planned times are sampled from the canonical
windows using the existing random-number behavior. Platform stopping lists are
populated from the resolved occurrence stops without invoking the
filesystem-era platform loader.

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

## Compatibility boundary

The loader accepts historical aliases for existing scenes, while the writer
emits only preferred V1 keys. The aliases and their exact mappings are listed
in [Scene Schema Reference](scene-schema.md#historical-compatibility-aliases).

`SceneImporter` maps the legacy seven-token train definition, physical and
traction source files, timetable rows, routes, stations, supported scenarios,
and passengers into the model. It is an explicit conversion command/UI action,
not a fallback used when running a scene. `SceneExporter` remains an explicit
interoperability tool; exporting does not change the active scene or stage the
normal simulator input.

The committed scenes contain only canonical structured data. Historical source
filenames remain as compact provenance in `source` and `import_report`; full
legacy trees are not duplicated.

## Bundle format (v2)

The transparent `.egscene` transport container packages the existing V1 JSON
files without changing `SceneModel` or the V1 schema. Its manifest and safety
rules are documented in [V2 Transparent Scene Bundle](scene-bundle.md).
Directories remain the editable source of truth; bundle loading extracts to a
temporary directory and reuses `loadScene`.

## Native runtime path

The infrastructure/signalling builder accepts an already loaded and validated
`SceneModel` and populates the existing runtime globals. The operations builder
then creates services, train occurrences, the selected scenario, entrance
delays, and passengers in memory. Each native `Train` carries operating code,
service ID, occurrence, performance, configured cap, composition maximum, and
applied maximum-speed provenance. `RunResults` copies that provenance to train
rows and operating code/service identity to timetable rows. `TrackPreview`
resolves connections and station markers through the same canonical node IDs.
GUI Run, `--scene`, and the numeric case shortcuts all enter this shared native
path; there is no legacy-runtime fallback.
