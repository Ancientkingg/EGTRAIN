# Using EGTRAIN and authoring V1 scenes

This guide covers canonical scene authoring: open a V1 directory or V2 bundle,
convert an external legacy case when needed, edit planned input, validate it,
and run it through the native scene path.

## Open and run

Choose `File > Open Case Study...` and select an `.egscene` file. Use
`File > Open Scene Folder...` when editing a canonical directory containing
`scene.json`. The **Loaded Data** dock opens with the case study so you can
review what was found before choosing `Run Scene`. Use
`Save Case Study As...` for a new bundle. From the command line, `--scene`
accepts the same bundle or directory path.

The application loads the six required JSON files, accepts optional scenarios,
passengers, display metadata in `views.json`, and historical
compatibility aliases, and validates before a run. Infrastructure, signalling,
rolling stock, services, the selected
scenario, and passengers are built directly from `SceneModel` in memory.

## Review what loaded

The **Loaded Data** dock keeps opening separate from running. Its case-study
tree shows the source path, canonical schema version, bundle format version,
source files, category counts, default and available scenarios, validation
state, and runtime/result readiness. Expand a category to follow source data to
parsed canonical objects. Imported scenes also show conversion, skipped, and
unresolved-reference counts from `import_report`. `Missing optional` means the
scene is usable without that file; `Unsupported` means the file was found but
EGTRAIN did not consume it. `Not built` means no runtime has been prepared from
the current input.

Double-click infrastructure or signalling rows to focus the existing network
view, validation rows to open the diagnostics table, or a train unit,
composition, service, or scenario incident to open its existing editor.
Each train-unit row owns its parameter, curve, plot, and provenance details.
Train-unit provenance is descriptive:
an original parameter or tractive-effort filename is not reopened by the native
runtime. The tractive-effort plot evaluates the same piecewise polynomial as the
runtime and displays speed in km/h and effort in kN.

## Scene directory

| File | Status | Contents |
| --- | --- | --- |
| `scene.json` | required | schema version, name, units, base time, simulation settings, import report |
| `infrastructure.json` | required | tracks, nodes, arcs, blocks, connections |
| `stations.json` | required | stations, positions, platforms, platform nodes |
| `signalling.json` | required | signals, routes, dependencies, restrictions, boundaries |
| `rolling_stock.json` | required | physical/traction train units and compositions |
| `services.json` | required | route/composition links and planned timetable stops |
| `scenarios.json` | optional on load; always written | default scenario, named scenarios, incidents, entrance delays |
| `passengers.json` | optional | journeys, absolute midnight-second windows, and legs |
| `views.json` | optional display metadata | authored track levels/regions and station display geometry; preserved in folders and bundles |

The writer emits preferred V1 keys. Stop plans use independently optional
`planned_arrival_seconds` and `planned_departure_seconds` on any stop, plus
required `dwell_seconds`. Legacy `-1` values remain omitted planned fields;
they are not treated as results or filled by a last-stop rule. See [V1 scene
properties](v1-scene-properties.md) for editing examples.

Scenario files use `default_scenario_id`, named `scenarios`, concrete
`incidents`, and `entrance_delays`. Passenger windows use absolute seconds
from midnight. The complete key contract and historical aliases are in the
[schema reference](../architecture/scene-schema.md).

## Scenario library and student loop

Open the **Incidents** dock to choose the canonical default or another named
scenario. The library shows each scenario's ID, name, incident count, default
and validation status, and an in-memory modified marker. **Blank** and
**Duplicate** create isolated scenarios; editing incidents, names, or
descriptions changes only the selected scenario. **Import JSON...** and
**Export JSON...** use the standalone scenario object documented in the schema
reference. Imports retain entrance delays, validate references against the
open case, and report any adjusted ID rather than replacing an existing
scenario or incident.

The completed student loop is: open a case, review **Loaded Data** and
**Validation**, choose or edit a scenario, save (or **Save Case Study As...**),
review the run summary, run the selected scenario, then use **Run Results** to
open the existing timetable, delay, speed, and blocking-time views. Results
are cleared when the case or scenario changes and are rebuilt only by a new
run, so a result window is never presented as belonging to a newly selected
scenario.

The incident editor exposes occurrence, reduced-speed cap, recovery end (or
until-destination), and destination termination directly; these are not hidden
JSON-only settings. A completed incident-free run can be frozen with **Set
delay baseline**. Selecting another scenario retains that baseline, while a
canonical scene edit, **New**, or **Open** clears it and advances the local
scene revision. **Compare delays** requires an incident run with no entrance
delays and matching scene revision, base time, duration, timestep, and full
selected `(service_id, occurrence)` identity set. The compact table and CSV
show baseline/scenario identities, matching final authored timetable arrivals,
positive contribution, primary/secondary attribution, incident IDs, first
direct time/location, and destination-termination outcome. Primary means direct runtime evidence exists
for that occurrence; secondary means it does not. Timetable differences alone
are never treated as causal, and a comparison with no direct evidence anywhere
is rejected. Positive contribution rows sum exactly to total arrival delay.

## Portable bundles

V2 `.egscene` files package canonical V1 JSON in a deterministic ZIP archive.
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
workflow and [Scene bundle format](../architecture/scene-bundle.md) for the
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

## Edit and validate

Edit canonical JSON or use the scene editor. Keep IDs unique and keep service
links consistent:

```text
service.composition -> rolling_stock.compositions[].id
service.route       -> signalling.routes[].id
stop.station        -> stations[].id
stop.platform       -> platform on that station
scenario incident   -> signal/block or service target as appropriate
```

`passengers.json` is optional canonical input. The Passengers dock edits its
journeys and ordered legs and can append the supported DAS/RouteChoice CSV pair.
The import result table identifies malformed, unresolved, accepted, and
ID-collision rows. The native runtime does not reopen those files. Random
passenger draws and simulation results are excluded from scene input. Likewise, legacy OL, TDS,
Rescheduling, and GUI files have their own active/inert/output classifications;
see the [migration matrix](v1-scene-migration-matrix.md) instead of inferring
behavior from a filename.

Named-scenario selection, entrance delays, incidents, and passengers execute
from canonical data. The runtime applies only the selected/default scenario.

Run the structural and semantic validation command after edits. The path can
name a scene directory or bundle:

```bash
./build/scene_tool validate path/to/scene
```

The command preserves support for structurally valid, incomplete historical
scenes. Run gating additionally uses `validateRunnableScene`; the equivalent
directory helper is `validateRunnableSceneDirectory`. Structural loading is
checked first so semantic reference diagnostics do not cascade from malformed
JSON.

Before committing a scene, validate it and run the native scene path. Use
`scene_tool export` only when a downstream legacy tool needs interoperability
files; edits to that export do not flow back into canonical JSON. The exporter
generates legacy infrastructure, signalling constraints, rolling stock,
timetables, and supported passenger CSVs from canonical data. It reports an
error when a canonical passenger window cannot be represented by the legacy
half-hour bucket format instead of silently changing it.

## Scope

The V2 bundle changes packaging only. V1 JSON and `SceneModel` remain the
simulation-data contract.
