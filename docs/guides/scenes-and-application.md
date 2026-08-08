# Using EGTRAIN and authoring V1 scenes

This guide covers the Milestone 1 scene workflow: open a canonical directory,
convert a legacy case, edit planned input, validate it, and run through the
existing compatibility path.

## Open and run

Start EGTRAIN from `EGTRAIN/QEGTRAIN`; legacy relative paths resolve from that
directory. Choose `File > Open Scene...` and select the directory containing
`scene.json`. Review validation diagnostics, use `Save Scene As...` for a
working copy, then choose `Run Scene`.

The application loads the six required JSON files, accepts optional scenarios,
passengers, views, and historical compatibility data, and validates before a
run. EGTRAIN exports the canonical fields already supported by the bridge,
including services, rolling stock, routes, and default-scenario incidents, to a
temporary legacy directory. It copies required passthrough data and starts the
existing simulator. This legacy runtime/export path remains in the PR.

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
| `views.json` | optional | display defaults |
| `legacy/` | needed by the current runtime as applicable | passthrough files used by legacy loaders |

The writer emits preferred V1 keys. Stop plans use independently optional
`planned_arrival_seconds` and `planned_departure_seconds` on any stop, plus
required `dwell_seconds`. Legacy `-1` values remain omitted planned fields;
they are not treated as results or filled by a last-stop rule. See [V1 scene
properties](v1-scene-properties.md) for editing examples.

Scenario files use `default_scenario_id`, named `scenarios`, concrete
`incidents`, and `entrance_delays`. Passenger windows use absolute seconds
from midnight. The complete key contract and historical aliases are in the
[schema reference](../architecture/scene-schema.md).

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
stations from the track-line station file. It keeps runtime support trees that
still need passthrough under `legacy/` and reports missing references,
malformed rows, and preserved source anomalies. In particular, a timetable
sentinel of `-1` means the corresponding planned arrival or departure is
absent; the importer neither synthesizes a result nor changes an inconsistent
source time.

Example:

```bash
./build/scene_tool import \
  EGTRAIN/QEGTRAIN/Input/Input_EGTRAIN_Netherlands \
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

`passengers.json` is optional and is not a substitute for the legacy runtime's
conditional DAS/RouteChoice CSV input. Random passenger draws and simulation
results are excluded from scene input. Likewise, legacy OL, TDS,
Rescheduling, and GUI files have their own active/inert/output classifications;
see the [migration matrix](v1-scene-migration-matrix.md) instead of inferring
behavior from a filename.

Named-scenario selection, canonical entrance-delay execution, and canonical
passenger execution are not enabled in Milestone 1. The bridge does not create
Rollout or passenger CSV files from JSON, so current runs still use those files
from `legacy/` when available. Canonical edits to those fields are persisted and
validated now; making the simulator consume them is later-milestone work.

Run the structural and semantic validation command after edits:

```bash
./build/scene_tool validate path/to/scene
```

The command preserves support for structurally valid, incomplete historical
scenes. Run gating additionally uses `validateRunnableScene`; the equivalent
directory helper is `validateRunnableSceneDirectory`. Structural loading is
checked first so semantic reference diagnostics do not cascade from malformed
JSON.

Before committing a scene, validate it, export to a clean temporary directory,
and run the compatibility path with its required `legacy/` data. Do not edit
an exported legacy directory expecting those changes to flow back into the
canonical JSON.

## Scope

This guide describes Milestone 1's canonical directory and legacy bridge. The
`.egscene` bundle format and native simulator input are outside this milestone.
