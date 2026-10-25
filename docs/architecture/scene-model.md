# V1 Scene Model

V1 is a directory of canonical JSON files. It is the only normal GUI, CLI,
preview, and simulation input:

```text
legacy case -> explicit SceneImporter -> canonical SceneModel
canonical files -> loadScene -> validation -> editor/preview
canonical model -> native infrastructure + operations -> simulator
canonical model -> explicit SceneExporter -> legacy interoperability files
```

[`DispatchController::prepareScene`] is the shared native setup seam. It consumes
the loaded model through the infrastructure/signalling and operations builders
and prepares runtime output directories. Neither builder opens provenance files
or consults an `Input/` tree.

The infrastructure builder checks the whole scene before it writes to the
run. If it still finds an error while it builds the runtime, it puts the
previous infrastructure back, so a build that returns an error leaves it in
place. Only the single-track locks of a run are cleared, as at the start of
every build.

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
| `views.json` | optional track levels, double switch drawing and station display geometry; consumed only by preview/runtime layout | optional |

The six required files are the files needed for structural loading. A scene
may therefore load without scenarios or passengers; the writer creates a
baseline `scenarios.json` even when there are no named scenarios. Runnable
validation additionally requires populated infrastructure, routes, rolling
stock, services, and positive simulation duration.

The writer emits JSON with four-space indentation, sorted keys, and a final
newline. It omits optional keys that are absent, always writes `scenarios.json`,
and writes `views.json` and `passengers.json` only when the scene has views or
passengers. A committed file in another form is rewritten by the next save. A
scene that this version of the writer has saved gives the same bytes when it is
loaded and saved again.

Each key of the scene files is written in one place and read in one place.
`SceneWriter.cpp` has an encoder for each file (`encodeSceneMetadata`,
`encodeInfrastructure`, `encodeStations`, `encodeSignalling`,
`encodeRollingStock`, `encodeServices`, `encodeScenarios`, `encodeViews` and
`encodePassengers`), and `SceneFieldReader.cpp` has a decoder for each file
(`decodeSceneMetadata`, `decodeInfrastructure`, `decodeStations`,
`decodeViews`, `decodeSignalling`, `decodeRollingStock`, `decodeServices`,
`decodeScenarios`, `decodeHistoricalIncidents` for `incidents.json` and
`decodePassengers`). `loadScene` in `SceneModel.cpp` reads and parses the
files, gives each file its own `SceneFieldReader` and calls the decoders in that
order. It also makes the checks that span files: it decodes `views.json` after
the tracks and stations that the views name, and `incidents.json` only when the
case has no `scenarios.json`; with both files it warns that `incidents.json` is
ignored. The importer fills a [`SceneModel`] and
hands it to `replaceSceneDirectory`, which writes, loads back and publishes it
as `saveScene` does, so an imported scene has the form of a saved one. Unlike a
save, it replaces a folder at the destination with everything in it and keeps
the default mode of a new folder.
The exporter writes the items of its area sidecar with `encodeSignallingArea`,
and the importer reads them with `decodeSignallingArea`.

`test_scene_codec_contract` (`tools/e2e/test_scene_codec_contract.py`) fails when
a key of the scene files is written outside `SceneWriter.cpp`, is read outside
`SceneModel.cpp` and `SceneFieldReader.cpp`, or when a file in `scene/` includes a
header of `simulation/`. It lists its exceptions with their reasons:
`SceneBundle.cpp` and `SceneCompatibility.cpp` read `schema_version` and
`saved_with_app_version` before the loader runs, and `SceneExporter.cpp` and
`SceneImporter.cpp` handle `signalling_areas` as the top-level key of the area
sidecar of a legacy folder and `base_time` and `simulation_settings` (with its
three settings) as the keys of the settings sidecar. The limits of the native runtime, which the validator
and the builders check against, are in `scene/RuntimeLimits.h`.

## Model ownership

`SceneModel` owns the canonical values, not a second flat copy of legacy
incidents. Incidents belong to a [`SceneScenario`]; the selected scenario is
identified by `default_scenario_id`. `sourceFiles`, the scene files present at
load or save, is derived and not saved. The Loaded Data tree is not part of the
model: `buildLoadedDataSummary` in `scene/LoadedDataSummary.h` derives its rows
from the model, its source files and the diagnostics of validation, and
`ValidationPanel` builds them again each time it writes the tree. Each row has a
`SceneLoadedStatus`, which the tree shows as `OK`, `Not present`, `Warning` or
`Invalid`. Loaded-data navigation targets point only to
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
advances. Repetition keeps the canonical entry in `scheduled_departure_time`,
and `departure_time` starts equal to it. Arrival and departure timetable values
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

`scene/SceneReferences` and `scene/SceneIds` hold the id rules of the editors, without Qt.
`sceneDirectDeleteConsumers` lists what still uses a track, node, block, connection, station,
platform, signal, route, train unit, composition or service, so that a delete can be refused with
that list. The `sceneRewrite...References` functions rewrite every reference to an entity after a
rename; the caller renames the entity itself. The `sceneUnique...Id` functions return the base id
when it is free and otherwise the first of `base_2`, `base_3` and so on.

`scene/SectionReference` reads a section reference, a block id or several joined by `/` such as
the two endpoints of a switch transition `@71-B0@-21.235000/@0-B1@-21.252000`.
`parseSectionReferencePart` splits one part into its block id and the offset of an endpoint. The
importer, the exporter, the validator, `SectionInventory` and the editors' reference rules all read
references with it, so they agree on which parts name a block and which routes hold a switch
transition. `nativeRuntimeBlockId` gives the runtime id of a block section.

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

### Edit history

`scene/SceneHistory` keeps whole-scene snapshots for Undo and Redo and has no Qt
dependency. `record` appends a copy of the scene and drops the snapshots after
the current position, so an edit after Undo abandons the Redo branch. `undo`
and `redo` move the position and return the snapshot that becomes the live
scene. Each snapshot carries a label that `record` derives from the counts of
the parts of the scene (`Add service`, `Delete stop`, or `Edit` when the change
is not one part added or removed), the scenarios that differ from the saved
scene, and its memory as `estimateSceneModelBytes` counts it. That estimate
adds up the structs, the vectors and the strings too long for the string
object, so it depends on the standard library and is a bound, not a
measurement.

The history keeps at most 100 undo steps and an estimated 128 MiB. Both limits
are parameters of the constructor. When either is exceeded the oldest
snapshots are dropped; the live one never is, even when it alone is larger than
the memory limit. One position can be marked as saved, by `markSaved` or by
`reset` for a scene that was just opened. The scene is clean exactly when the
position is the saved one. A saved snapshot that is dropped, by the abandoned
Redo branch or by a limit, leaves no saved position, and the scene stays dirty
until the next save. `reset` starts a new history from a scene that was opened,
created or recovered.

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
transactional writer. Before the copy is written, the upgraded scene is loaded
and checked with `validateScene`, the semantic checks and not the runnable
ones. An error stops the upgrade and no copy is written; warnings do not stop
it. The source directory or bundle remains unchanged on success and failure.
Directory scenes and transparent bundles share the same current model, and a
newer EGTRAIN release is required for newer formats.

The loader accepts historical aliases for existing scenes, while the writer
emits only preferred V1 keys. The aliases and their exact mappings are listed
in [Scene Schema Reference](../guides/scene-schema.md#historical-compatibility-aliases).

`SceneImporter` maps the legacy seven-token train definition, physical and
traction source files, timetable rows, routes, stations, supported scenarios,
and passengers into the model. It is an explicit conversion command/UI action,
not a fallback used when running a scene. A legacy number that reads as `nan`
or `inf` is malformed, like any other unreadable number: a row of a file is
skipped with the warning `scene.import.parse`, and a Trains or train data file
gives that code as an error and stops the import. `SceneExporter` remains an explicit
interoperability tool; exporting does not change the active scene or stage the
normal simulator input.

A legacy file that cannot be opened, or that starts with the UTF-16 byte order
mark and has an odd byte count or an unpaired surrogate, is reported instead of
read as empty. The train, train data, traction, route, timetable and passenger
files and the two sidecars of an export keep their own diagnostics.
`Stations.txt` gives the error `scene.import.read`
and stops the import, as a missing one does. Any other file gives the warning
`scene.import.read` and counts as skipped in `import_report`. An unreadable
`trainNames.txt` is replaced by the files in `Trains`, as a missing one is, and
an unreadable rollout adds no scenario.

`SceneExporter` writes a legacy case into a staging folder next to the
destination and publishes it only when the export is complete, so a failed
export leaves the destination as it was. A new destination is the staging
folder renamed. Whether a file is still to be written is decided from the
staging folder alone: the export copies the files of the legacy passthrough
first and then writes the files that are still missing, such as the track line
files, `Stations.txt`, `Connections.txt`, the GUI layout, the route corridors,
the single-track limits, the station boundaries and the passenger files. In an
existing destination the publish first removes every input of the importer that
the destination holds and the staging folder does not, and then moves the staged
files over the files of the same name. `kLegacyInputs` in `SceneExporter.cpp`
names these inputs, in any letter case: the track folders `B<n>` in `TrackLines`
or in the case folder, `Connections.txt`, `Stations.txt`, `AreasCaseStudy.txt`
and the area sidecar in `TrackLines`, `Stations.txt`,
`EGTRAINInitialParameters.json`, `trainNames.txt` and `Incidents.txt` in the
case folder, the files `Route<N>.*` in `Routes`, the `.txt` files in `Trains`,
the six GUI files, `RoutesToWrite/RoutesToJoin.txt`, the two passenger files in
`Passengers` or in the case folder, and the files `Rollout_<N>.txt` in
`TimeTable/Scenarios_Entrance_Delays`. A track folder goes with everything in
it. The area sidecar goes first, also when the export writes it again, so that a
publish that fails part way never leaves an old sidecar beside new track files.
So the published folder imports as the new scene: a scene with fewer tracks,
without corridors or without passengers leaves no old track folder, corridor
file or passenger file behind. The train data and timetable files are read only
through the paths in the train files, which name only files of this export, so
they are not removed. Every other file and folder in the destination stays, and
so do the files in a track folder that the export writes again.

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
`signalling.json`, the service keys `category` and `visualization_color`, and
`double_switch` in the track views of `views.json`. None of them changed a
number.

The probe reads `scene.json` (directory) or the bundle manifest, and, only
when `scene.json` has a `legacy_source` key, `infrastructure.json`. A scene
with `schema_version` 1, and `bundle_version` 1 in a bundle, is current unless
it has the draft layout described in the table. The table lists the classes of
scene and what the application does when one is opened from its window.
Automated runs do not show the dialogs.

A scene that the probe rejects as older or newer carries one error diagnostic
whose message gives the schema number, the bundle layout number of a bundle,
the `saved_with_app_version` and the numbers this version reads. A scene that is
current or that a migration can convert carries none. `scene_tool validate` and
`pack` and the headless start probe the path first and report this diagnostic,
not the loader's errors, and exit with code 1. A window start with a file name
reports it in the message **Cannot Open Case** and then starts as without
a file name. A window start with `--scene` gives such a scene to the window
without loading it, prints the diagnostic and shows the dialog from the table;
it exits with code 1 when the scene is not opened.

After a clean load, `scene_tool validate` and the `--scene` option also run the
semantic checks (the runnable checks for `--scene` and for `validate
--runnable`) and exit with code 1 on any error. A scene whose runnable checks
report errors therefore opens in the window with Run disabled, but `--scene`
refuses to start on it: the headless start prints the diagnostics, and the
window start first shows "Cannot Start EGTRAIN".

| Class | How it is recognised | What the application does |
| --- | --- | --- |
| Current layout | Directory: `schema_version` 1 in `scene.json`, with the railway in `infrastructure.json` (nodes, arcs and the other sections of the [Scene Schema Reference](../guides/scene-schema.md)), written by build main-81.1 or later. Bundle: `format` `"egscene"`, `bundle_version` 1 and `schema_version` 1 in the manifest, written by build main-82.1 or later. The Paimpol scene of main-81.1 and the released Paimpol bundles of main-82.1, production-107.1 and v1.0.2 have these numbers and pass `scene_tool validate` without errors. Builds main-78.1 to main-80.1 wrote the keys of this layout but shipped their case studies in the draft layout; no scene saved by them was checked, so they are in neither class. | Supported. Opens directly, with no copy and no compatibility dialog. The loader also accepts the historical aliases listed in [Scene Schema Reference](../guides/scene-schema.md#historical-compatibility-aliases). |
| Draft layout | Saved by builds before the canonical model of 2026-08-08 (main-77.1 and earlier). A `scene.json` with a `legacy_source` key and an `infrastructure.json` with no nodes and no arcs; the simulator read the railway from a `legacy/` text tree. `schema_version` is 1, like today's, so the number does not separate these scenes. Neither sign identifies the layout alone: a current scene may carry a `legacy_source` key, and an empty `infrastructure.json` alone is left to the loader. | Deliberately unsupported: these scenes are not converted and no migration is provided, also when one is registered for the number. The probe classes the scene as unsupported with the diagnostic `scene.compatibility.draft`, which names the draft layout and the date and says to import the original legacy case again. The dialog "Older Case Not Supported" says `This case is too old for this version of EGTRAIN to upgrade.` and then gives that message, and `--scene` and `scene_tool` exit with code 1 and print it. Use the current case studies, or convert the original legacy input folder again with **File > Import Legacy Case...** or `scene_tool import`. The Paimpol scene of main-77.1 is stored as a fixture. |
| Number below 1 | `schema_version` below 1 in `scene.json`; in a bundle, `schema_version` or `bundle_version` below 1. | Deliberately unsupported. The scene is not opened and the dialog is titled "Older Case Not Supported". It says `This case is too old for this version of EGTRAIN to upgrade.` and then gives the diagnostic `scene.compatibility.older` with the numbers of the scene and of this version. |
| Newer | `schema_version` above 1 (directory or bundle), or `bundle_version` above 1 (bundle). | Never downgraded. The scene is not opened; the dialog is "Newer Case", which says `This case was saved by a newer version of EGTRAIN.` and `Update EGTRAIN to open it.`, gives the scene schema and, for a bundle, the bundle layout against those of this version in its details, and offers **Check for Updates...** and **Cancel**. `--scene` and `scene_tool` print the diagnostic `scene.compatibility.newer` with the numbers of the scene and of this version. |
| Unreadable | `schema_version` (in a bundle also `bundle_version`) missing, not an integer, or outside the integer range; a `saved_with_app_version` that is not a string; a directory without a readable `scene.json`; in a bundle also a `format` other than `"egscene"`, a bad manifest, a missing `scene.json`, an unsafe archive, and, for `bundle_version` 1, an entry that is not part of the format or another missing required entry. | The scene is not opened. The dialog is "Cannot Open Case". Its text gives the file name and a sentence that the file is damaged or is not an EGTRAIN case (for a folder, that the folder holds no EGTRAIN case or that its `scene.json` is damaged), then the first diagnostic. The same title and text are used when a scene passes the probe and the loader then reports errors; when the loader reports more than one error, the diagnostic then ends with "1 more error" or "N more errors", so the title alone does not mean that a version is unreadable. A window start with a file name shows the same message. |
| Upgradable | A number below the current one for which the registry holds a migration path. No such number exists today: the production registry is empty. | The dialog is "Older Case": it says `<file> was saved by an older version of EGTRAIN.` and `Upgrade a copy to open it. The original stays as it is.`, gives the numbers in its details and offers **Upgrade a Copy...** and **Cancel**; the upgrade writes a copy and leaves the original unchanged. The registry is empty because every scene written so far has version number 1; neither a migration nor a no-op migration is registered. |

A scene with one number above 1 and the other below 1 is treated as newer.

A version number changes only for a change that makes existing scenes
unreadable. The change that does it registers the migration step for the
previous number and adds a scene of that number as a test fixture, in the same
change. Optional keys, optional files and new validation warnings never change
a number. `test_sceneversions` enforces the rule: raising `schema_version` or
`bundle_version` makes it fail until the fixtures in
`tests/fixtures/scene-versions` are reconsidered. A number below the current one
passes only with a fixture of that number and either a registered migration for
it or a manifest entry that lists every fixture of that number as deliberately
unsupported.

## Bundle format (v2)

The `.egscene` transport container, its manifest, its safety rules and how it is read and written are described in
[V2 Transparent Scene Bundle](../guides/scene-bundle.md).

## Native runtime path

The infrastructure/signalling builder accepts an already loaded and validated
`SceneModel` and populates the run that it is given. The operations builder
then creates services, train occurrences, the selected scenario, entrance
delays, and passengers in memory. Each native [`Train`] carries operating code,
service ID, occurrence, performance, configured cap, composition maximum, and
applied maximum-speed provenance. [`RunResults`] copies that provenance to train
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
level are described in [Signalling levels](../guides/signalling-levels.md).

[`DispatchController::prepareScene`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_dispatch_controller.html
[`SceneModel`]: https://ancientkingg.github.io/EGTRAIN/latest/api/struct_scene_model.html
[`SceneScenario`]: https://ancientkingg.github.io/EGTRAIN/latest/api/struct_scene_scenario.html
[`Train`]: https://ancientkingg.github.io/EGTRAIN/latest/api/class_train.html
[`RunResults`]: https://ancientkingg.github.io/EGTRAIN/latest/api/struct_run_results.html
