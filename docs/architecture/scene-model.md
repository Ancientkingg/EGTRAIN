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
| `signalling.json` | signals, routes, signalling areas, dependencies, single-track restrictions, and station boundaries | required |
| `rolling_stock.json` | train units, physical data, traction curves, and compositions | required |
| `services.json` | services, explicit route/composition links, stops, planned times, dwell, and repetition | required |
| `scenarios.json` | default scenario, named scenarios, incidents, and entrance delays | optional on load; always written by `SceneWriter` |
| `passengers.json` | passenger journeys and legs | optional |
| `views.json` | optional track levels and station display geometry; consumed only by preview/runtime layout | optional |

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

Planned times on the route must be finite, non-negative and ordered, including
arrival-only rows. Explicit entry must precede the first known route event.
Without explicit entry, the native first-departure rule permits an earlier
origin arrival for pre-departure dwell. Invalid numbers and chronology block
Run; incomplete intermediate departures and insufficient dwell windows remain
warnings for historical schedules. These checks do not predict physical running
time. Inert off-route context retains historical finite negative offsets.

Validation also warns, once per route and composition used by a service, when
arcs of the route are steeper than the composition can brake on or start on
(`scene.route.gradient.steep`). The warning does not block Run.

Runnable validation also warns, once per single-track restriction, when its
start, end or protected blocks have no signalling level
(`scene.single_track.no_effect`): the restriction then has no effect at run
time. The warning does not block Run.

The stop list is authoritative: empty means no scheduled calls, regardless of
the historical `through` field. Nonempty stops are never discarded because that
field is true. Canonical input/output retains the field for compatibility;
legacy export writes the actual stop rows or an empty timetable, which import
recognizes as a through service.

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

The optional service `category` is descriptive metadata, independent of physical
properties, performance, speed limits and stops. Missing values mean no category;
unknown strings survive canonical folder and bundle persistence. Legacy export
cannot represent this field and omits it.

The optional service `visualization_color` is a display colour written as
`#RRGGBB`. It has no effect on the simulation. The model keeps the text as
loaded and the writer omits it when empty. Folder and bundle persistence
preserve it; legacy export omits it. A non-empty value in any other form
gives the warning `scene.service.color.invalid`, which does not block Run, and
the default train colour is used for that service.

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

Route descriptions, stop validation and native stop preparation share an ordered
traversal from `SectionInventory`. It follows authored section order, native
direction and overlapping-switch clipping. Stops consume successive real node
visits; shared section boundaries count once, while later visits are retained.
A missing platform resolves only when one reachable platform remains. Explicit
unreachable platforms and exhausted/out-of-order visits block preparation.
Blank-platform rows at stations outside the route remain warning-labelled inert
schedule context for legacy compatibility. No provenance flag distinguishes
imported rows from otherwise identical authored rows.

`sceneRouteStations` lists the stations of a route from that traversal in travel
order, counting consecutive visits of one station once. The service editor's
route choices and the reference-route chooser of the diagrams use it.

`scene/StopInsertion` adds a stop at a chosen position of a timetable without
disturbing the others. `sceneStopInsertionWindow` returns the route visits
that lie strictly between the neighbouring stops: they start after the last
resolved stop before the position and end at the visit of the first resolved
stop at or after it, resolved over the whole timetable. The stations of those
visits, and their platforms, are the choices for the new stop, so a station
whose only visit lies after the next stop is not offered. A stop before the
position that is neither resolved nor off-route context leaves no window and is
named in the result. `insertSceneStop` resolves a copy of the service with the
new stop and inserts it only when the new stop resolves and every stop that
resolved before still resolves to the same route visit; otherwise the service
is unchanged and the error names the stops concerned. Two stops with the same station and platform inside
one window bind to its visits in order. `sceneServiceTraversal` and
`sceneRemainingStopTraversal` build the traversal and the visits left after the
resolved stops before an index.

Scheduled entry uses explicit entry time first, otherwise the first finite
planned departure, otherwise zero, plus the repeat offset. The editor's
in-period count includes entries in `[0, effective duration)`. Configured totals
and selected totals remain separate; this display does not filter runtime
expansion or include scenario entrance delays in the schedule.

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

Compatibility is probed from `scene.json` before normal loading. The schema
version describes canonical data; the bundle version describes ZIP transport;
`saved_with_app_version` is descriptive provenance only. A newer schema or
bundle is never downgraded. Older inputs are migratable only through explicit
incremental registry steps; the production registry is currently empty.

An upgrade always writes a copy through private staging and the existing
transactional writer. The source directory or bundle remains unchanged on
success and failure. Directory scenes and transparent bundles share the same
current model, and a newer EGTRAIN release is required for newer formats.

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

### Scene versions in use

Every build so far has written `schema_version` 1, and `bundle_version` 1 in a
bundle. The loader has accepted no other value since it was introduced
(`loadScene` in `SceneModel.cpp`, `loadSceneBundle` in `SceneBundle.cpp`), so no
older version number exists. The older scenes that exist differ from today's in
layout, not in number. The format has grown only by optional keys and optional
files, such as `views.json`, `saved_with_app_version`, `signalling_areas` in
`signalling.json`, and the service keys `category` and `visualization_color`.
None of them changed a number.

The probe reads only `scene.json` (directory) or the bundle manifest. A scene
with `schema_version` 1, and `bundle_version` 1 in a bundle, is current
whatever its layout. The table lists the classes of scene and what the
application does when one is opened from its window. Automated runs do not
show the dialogs.

`scene_tool validate` and the application's `--scene` option load the path
before any probe, so a version number outside the supported range is reported as
a loader error and not as one of these dialogs. After a clean load they also run
the semantic checks (the runnable checks for `--scene` and for `validate
--runnable`) and exit with code 1 on any error. A scene whose runnable checks
report errors therefore opens in the window with Run disabled, but `--scene`
refuses to start on it: the headless start prints the diagnostics, and the
window start first shows "Cannot Start EGTRAIN".

| Class | How it is recognised | What the application does |
| --- | --- | --- |
| Current layout | Directory: `schema_version` 1 in `scene.json`, with the railway in `infrastructure.json` (nodes, arcs and the other sections of the [Scene Schema Reference](scene-schema.md)), written by build main-81.1 or later. Bundle: `format` `"egscene"`, `bundle_version` 1 and `schema_version` 1 in the manifest, written by build main-82.1 or later. The Paimpol scene of main-81.1 and the released Paimpol bundles of main-82.1, production-107.1 and v1.0.2 have these numbers and pass `scene_tool validate` without errors. Builds main-78.1 to main-80.1 wrote the keys of this layout but shipped their case studies in the draft layout; no scene saved by them was checked, so they are in neither class. | Supported. Opens directly, with no copy and no dialog. The loader also accepts the historical aliases listed in [Scene Schema Reference](scene-schema.md#historical-compatibility-aliases). |
| Draft layout | Saved by builds before the canonical model of 2026-08-08 (main-77.1 and earlier). `infrastructure.json` has empty `nodes` and `arcs`; the simulator read the railway from a `legacy/` text tree. Scenes made by the importer of those builds also have a `legacy_source` key in `scene.json`. `schema_version` is 1, like today's, so the number does not separate these scenes. Neither `legacy/` nor an empty `infrastructure.json` identifies the layout alone: a current scene may carry a `legacy/` directory, and no code reads `legacy_source`. | Deliberately unsupported: these scenes are not converted and no migration is provided. No code recognises the layout, so the probe calls such a scene current and the loader decides; the outcome depends on the content. The Paimpol case study of main-77.1 loads without loader errors, the validator reports errors for it (for example `scene.ref.unresolved` in `signalling.json`), so the window opens it with Run disabled, while `--scene` and `scene_tool validate` exit with code 1 on it; removing `legacy_source` and `legacy/` changes none of this. When the loader does report errors, the dialog is "Cannot Open Scene" with the first error and "Error count: N". Use the current case studies, or convert the original legacy input folder again with **File > Load Legacy Case...** or `scene_tool import`. |
| Number below 1 | `schema_version` below 1 in `scene.json`; in a bundle, `schema_version` or `bundle_version` below 1. | Deliberately unsupported. The scene is not opened and the dialog is titled "Older Scene Not Supported". |
| Newer | `schema_version` above 1 (directory or bundle), or `bundle_version` above 1 (bundle). | Never downgraded. The scene is not opened; the dialog is "Newer Scene" with **Check for Updates...** and **Cancel**. |
| Unreadable | `schema_version` (in a bundle also `bundle_version`) missing, not an integer, or outside the integer range; a `saved_with_app_version` that is not a string; a directory without a readable `scene.json`; in a bundle also a `format` other than `"egscene"`, a bad manifest, an unsafe archive, and, for `bundle_version` 1, an entry that is not part of the format or a missing required entry. | The scene is not opened. The dialog is "Cannot Open Scene" with the first diagnostic. The same title is used when a scene passes the probe and the loader then reports errors; the text then ends with "Error count: N", so the title alone does not mean that a version is unreadable. |
| Upgradable | A number below the current one for which the registry holds a migration path. No such number exists today: the production registry is empty. | The dialog is "Older Scene" with **Upgrade a Copy...** and **Cancel**; the upgrade writes a copy and leaves the original unchanged. The registry is empty because the inventory above found nothing to convert, so neither a migration nor a no-op migration is registered. |

A version number changes only for a change that makes existing scenes
unreadable. The change that does it registers the migration step for the
previous number and adds a scene of that number as a test fixture, in the same
change. Optional keys, optional files and new validation warnings never change
a number. This is a rule for changes to the format.

## Bundle format (v2)

The transparent `.egscene` transport container packages the existing V1 JSON
files without changing `SceneModel` or the V1 schema. Here "V2" names the
container and "V1" the data schema; the container's own number is
`bundle_version` 1, and the data inside has `schema_version` 1. Its manifest and
safety rules are documented in [V2 Transparent Scene Bundle](scene-bundle.md).
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
path; there is no legacy-runtime fallback. When `views.json` is present, the
builder applies authored track levels/regions and station coordinates before
`setupGUI`; scenes without it use the automatic layout fallback.

Canonical signalling areas assign levels to the base and derived switch
sections before route construction copies those sections. An area has a
complete chainage span, a level from 0 through 5, and optional canonical track
scope. Track-scoped areas override a network-wide area; conflicting values for
one section are rejected. Missing coverage keeps the native unset value, and
runnable validation warns about the route sections it leaves unset. Normal
runtime does not read `TrackLines/AreasCaseStudy.txt` and does not synthesize a
conventional or ETCS default. The levels and the rule that gives a section its
level are described in [Signalling levels](signalling-levels.md).
