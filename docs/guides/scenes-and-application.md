# Using EGTRAIN and authoring V1 scenes

This guide covers canonical scene authoring: open a V1 directory or V2 bundle,
convert an external legacy case when needed, edit planned input, validate it,
and run it through the native scene path.

## Open and run

Choose `File > Open Case Study...` and select an `.egscene` file. Use
`File > Open Scene Folder...` when editing a canonical directory containing
`scene.json`. Normal mode keeps the canvas and editor flow uncluttered; use
`View > Advanced / Developer details` when you want the automatic Loaded Data
panel, full validation counts, and technical inventories. The existing View
actions can still open Loaded Data or Validation explicitly in normal mode.
Use
`Save Case Study As...` for a new bundle. From the command line, `--scene`
accepts the same bundle or directory path.

The application loads the six required JSON files, accepts optional scenarios,
passengers, display metadata in `views.json`, and historical
compatibility aliases, and validates before a run. Infrastructure, signalling,
rolling stock, services, the selected
scenario, and passengers are built directly from `SceneModel` in memory.

### Application updates

At first launch EGTRAIN asks whether to check for releases automatically. You
can disable or re-enable automatic checks from **Help**. **Check for Updates...**
always performs a manual check, even when automatic checks are disabled. An
available update is downloaded only after consent; **Update and Restart**
installs it and relaunches the application. If the install location is not
writable, download the release and install it with the platform's normal
permissions instead.

An update is prepared in a hidden `.qegtrain-update-*` folder next to the
installation, and the updated application starts in its own directory. After a
successful update the folder is removed. On Windows the update helper cannot
delete itself, so the application removes the rest about 20 seconds after it
started, or at its next start when it was closed before that. Folders left by an
interrupted update are removed at a start of the application once they are older
than an hour.

Production updates are stable versioned releases. With automatic checks enabled,
EGTRAIN offers newer releases on the next application start; it does not interrupt
an already-running session when a release is published. Use **Help > Check for
Updates...** to check immediately. Installing an update always requires consent.

Production build 107 and earlier have no updater and need one manual upgrade.
The Linux AppImage from production build 111 also needs one manual upgrade to
correct its update-staging path. After that, use **Update and Restart** for
future releases. Windows and macOS build 111 can update in place.

Application version, scene schema version, and bundle version are independent.
The saved-with application version is provenance and does not by itself prompt
for an upgrade. An upgrade to a copy is offered only for a scene version that
has a registered migration, and none exists today; when one is offered, the
original directory or ZIP remains unchanged. Newer schema or bundle versions
require a newer app. The [Compatibility boundary](../architecture/scene-model.md#compatibility-boundary)
lists which scenes open.

## Review what loaded

The **Loaded Data** dock stays separate from running. Its case-study tree shows
the source path, canonical schema version, bundle format version, source files,
category counts, default and available scenarios, validation state, and
runtime/result readiness. Expand a category to follow source data to parsed
canonical objects. Imported scenes also show conversion, skipped, and
unresolved-reference counts from `import_report`. `Missing optional` means the
scene is usable without that file; `Unsupported` means the file was found but
EGTRAIN did not consume it. `Not built` means no runtime has been prepared from
the current input. Advanced mode raises this panel automatically after open;
normal mode leaves it available through View when you need it.

Double-click infrastructure or signalling rows to focus the existing network
view, validation rows to open the diagnostics table, or a rolling stock unit,
composition, service, or scenario incident to open its existing editor.
Each rolling-stock-unit row owns its parameter, curve, plot, and provenance details.
Rolling-stock-unit provenance is descriptive:
an original parameter or tractive-effort filename is not reopened by the native
runtime. The tractive-effort plot evaluates the same piecewise polynomial as the
runtime and displays speed in km/h and effort in kN.

The rolling-stock editor also has explicit **Link parameter file...** and
**Link traction file...** actions. These links are session-only absolute local
paths, separate from the descriptive provenance fields, and are cleared when a
scene is reopened or replaced; Save As does not relocate or persist them.
Parameter files contain exactly nine whitespace-separated finite values:
traction-unit mass (kg), wagon mass (kg), wagon count, maximum speed (m/s),
maximum deceleration (m/s²), frontal area (m²), resistance coefficient, jerk
(m/s³), and length (m). Traction files contain up to 20 nonblank rows, each
with five finite values: starting speed (m/s), ending speed (m/s), and the
coefficients `a b c` for force in N, `a + b*v + c*v*v`. Speed intervals must
have positive width, be ordered, and not overlap.

Each linked update is applied only after the complete candidate validates.
Malformed or missing content leaves accepted values unchanged and keeps
**Retry** available. A valid change marks the scene dirty only when values
differ and refreshes open input traction plots. Conflicting local edits require
a Keep local/Reload decision, grouped for all units sharing the file. Updates
observed during a run are offered after the run completes. Completed result
charts do not change.

## Scene directory

| File | Status | Contents |
| --- | --- | --- |
| `scene.json` | required | schema version, name, units, base time, simulation settings, import report |
| `infrastructure.json` | required | tracks, nodes, arcs, blocks, connections |
| `stations.json` | required | stations, positions, platforms, platform nodes |
| `signalling.json` | required | signals, routes, signalling areas, dependencies, restrictions, boundaries |
| `rolling_stock.json` | required | physical/traction rolling stock units and compositions |
| `services.json` | required | route/composition links and planned timetable stops |
| `scenarios.json` | optional on load; always written | default scenario, named scenarios, incidents, entrance delays |
| `passengers.json` | optional | journeys, absolute midnight-second windows, and legs |
| `views.json` | optional display metadata | authored track levels/regions and station display geometry; preserved in folders and bundles |

Signalling areas in `signalling.json` give runtime sections their signalling
level; see [Signalling levels](../architecture/signalling-levels.md).

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
scenario. The library shows each scenario's ID, name, incident and entrance-
delay counts, default marker, and an in-memory modified marker. The authored
description remains available; Advanced mode adds the full validation status.
Normal mode still marks an actionable invalid scenario as `Invalid` but does
not surface non-blocking warning detail. **Blank** and
**Duplicate** create isolated scenarios; editing incidents, names, or
descriptions changes only the selected scenario. **Import JSON...** and
**Export JSON...** use the standalone scenario object documented in the schema
reference. Imports retain entrance delays, validate references against the
open case, and report any adjusted ID rather than replacing an existing
scenario or incident.

The completed student loop is: open a case, optionally review **Loaded Data**
and **Validation**, choose or edit a scenario, save (or **Save Case Study As...**),
review the run summary, run the selected scenario, then use **Run Results** to
open the existing timetable, delay, speed, and blocking-time views. Results
are cleared when the case or scenario changes and are rebuilt only by a new
run, so a result window is never presented as belonging to a newly selected
scenario.

The incident editor exposes occurrence, reduced-speed cap, recovery end (or
until-destination), and destination termination directly; these are not hidden
JSON-only settings. During playback, choose a train in **Train to follow** and
activate **Follow** to center the network view on it immediately, including
while paused.

The list names the state of each train. A train that has not entered the network
shows its scheduled entry time, as in "Rail-1 (scheduled 08:25:50)"; the time is
the schedule, not the actual entry. After that a train is "(running)",
"(finished)", "(hidden)" while the Trains layer is off, or "(no position)" when
it has none to draw. The label at the right of the status bar says in one
sentence whether the selected train is followed and why not, and the tooltips of
**Follow** and of the list say the same. A train that has not entered yet remains
armed: the view does not move, and Follow starts when the train enters. While the
Trains layer is off the view does not move either, and it returns to the train
when the layer is switched on. Before a case has run, and in a case without
services, **Follow** is disabled and its tooltip says why.

In a live run Follow is switched off when the selected train leaves the network,
and a train that has left cannot be chosen. In the replay of a completed run
Follow stays on after the selected train has left, and continues when you go back
in time. Follow is also cleared when it is disabled, when playback exits, or when
the selected train is no longer available; it never silently switches to another
train.

Click a track, connection, node, station, signal, train or passenger on the
canvas to select it, in the preview as well as during a run. The matching
inspector opens and a light blue ring marks the item. The ring keeps its size at
every zoom and neither covers nor recolours the item. It follows a moving train and
disappears with the layer of its item. Clicking another item moves the ring;
clicking an empty part of the canvas or closing the inspector removes it.
Dragging the canvas pans the view and keeps the selection.

Signal heads on the canvas show what the simulation holds for the displayed
time: Stop (red, with a dark bar), Caution (yellow, with a dark dot), Proceed
(green), Unavailable (an empty gray ring) or Failed (a red lamp with a white
cross). A head is unavailable when its section has no signalling area, so a scene
without one shows rings for the whole run. A head is failed on every signal of a
section for as long as a signal failure incident is active. The marks appear when
a head is at least 6 pixels wide; zoom in to see them. While a run is paused the
canvas shows the last snapshot the simulation delivered, and after the run the
replay slider shows the heads of the selected time. The tooltip and the signalling
inspector name the state, and the Map key lists all five.

A completed run without incidents or entrance delays can be frozen with **Set
delay baseline**. The results panel confirms the completed run identity and
places the next action or disabled reason beside the baseline controls.
Selecting another scenario keeps the baseline but reports that the newly
selected scenario has not been run; a canonical scene edit, **New**, or
**Open** clears the baseline and explains that a new run without incidents or entrance delays is
needed. **Compare delays** requires an incident run with no entrance delays and
matching scene revision, base time, duration, timestep, and full selected
`(service_id, occurrence)` identity set. The compact table and CSV show
baseline/scenario identities, matching final authored timetable arrivals,
positive contribution, primary/secondary attribution, incident IDs, first
direct time/location, and destination-termination outcome. Primary means direct
runtime evidence exists for that occurrence; secondary means it does not.
Timetable differences alone are never treated as causal, and a comparison with
no direct evidence anywhere is rejected with a diagnostic. A valid comparison
with zero positive rows is still a success and is labelled **zero positive
additional final-arrival delay**; positive contribution rows otherwise sum
exactly to total arrival delay.

### Timetable graphs and train paths

These two **Diagrams** and **Run Results > Open result view...** actions use the
same reference-route distance axis, but show different data:

- **Timetable graph (planned vs simulated stops)** connects available planned
  and simulated station arrivals and departures in separate series and shows
  dwell at each stop as a vertical segment. Planned lines are dashed, simulated
  lines solid. Connections between stops are not continuous train movement.
- **Train paths (simulated movement)** plots recorded position samples against
  simulation time. It does not plot planned timetable events.

Both ask for a reference route in a list of the routes used in the run. Each row
shows the route id and the stations the route passes in travel order, for
example `route0 --> Gvc - Gdg - Ut`. The list uses station ids; hover a row for
the full text; a very long row is shortened in the middle. A route whose station
order cannot be resolved shows `(station order unavailable)` and one without
stations shows `(no stations on this route)`; both can still be chosen.
Double-click a row, or select it and press Enter, to open the diagram.

Distance increases to the right; time increases downward, with clock labels
offset by the case base time (elapsed `0 s` is run start). Other routes are
projected using shared node/station anchors. Ambiguous or unmapped portions are
omitted without extrapolation; lines break at missing events or unprojectable
samples. Train filtering, selection, zoom, and CSV/PNG
export remain available. Diagrams have no Technical details panels. A concise,
word-wrapped warning strip above the plot keeps scientific qualifications
separate from the bounded run/reference identity context.

Blocking-time diagrams show calculated envelopes, not independently observed
occupation. The envelope extends from approach minus setup and sight reaction
through clearance plus release and run margin. Incomplete, missing-clearance,
or unprojectable blocks are omitted. Recorded movement is sampled only within
the scoped envelopes; planned station references are a separate dashed layer.
Compressed blocking-time diagrams shift calculated envelopes, not recorded
movement. Neither view extrapolates missing route projections.

Input traction plots show rolling-stock input, not simulated effort. Their
warning remains visible when a curve contains negative effort below the default
`0 kN` lower bound; those values remain in the input, not removed or clamped by
the plot.

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

A `TrackLines/AreasCaseStudy.txt` is reported in `import_report` and in a
warning, but its rows are not converted: add the signalling areas in
Infrastructure > Signalling area.

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

In Services, Category offers Intercity, Regional, High speed/international,
Freight, Metro/urban, Suburban and No category. Unknown imported values remain
visible until you choose a replacement. This label does not change train
performance, composition or stops. It survives duplication and folder/bundle
saves, but not legacy export.

Service visualization colour, directly below Category, sets the colour of the
service's trains. Choose... opens a colour dialog; Default removes the colour.
Without a colour, every train is drawn in the default yellow. The colour applies
to every occurrence of the service (201-1, 201-2, ...) on the canvas and in
replay, and the Map key lists it with the service id. In the result windows of
a run, the lines of those trains in the speed, time, distance and tractive
effort diagrams, the delay diagram, the timetable graph and the train-path
graph take the colour with their line widths and dash patterns unchanged, and
the train filter shows it as the swatch of each train; the timetable table
shows the swatch too. Trains without a colour keep the chart colours. The
blocking-time charts keep the colours that show the block types and show the
service colour in the train filter only. A stored value that is not
a `#RRGGBB` colour is shown as "Invalid: " followed by the text and is kept until
you choose a colour or press Default; trains of that service use the default
yellow. Changing the colour counts as a scene edit, so it discards the run
results and replay like any other service edit. It survives duplication and
folder/bundle saves, but not legacy export.

The route chooser shows endpoints and direction. Its tooltip lists stations
passed by the route, not stopping calls. Add Stop chooses a remaining station
visit and its unique reachable platform; if several platforms are reachable,
choose one explicitly. Moving stops or changing routes can leave invalid
assignments: the editor keeps them for repair and explains the problem. These
drafts can be saved, but incompatible stop instructions cannot run. Historical
blank-platform rows outside the route remain schedule context, not simulated
stops.

Click a timetable row to edit its station, platform, dwell, arrival and departure.
Accept applies the fields together; Cancel leaves the row unchanged. Add Stop
uses the same dialog with an Insert position chooser: "At the start of the
timetable" or "After stop k" for each stop, so a station that occurs twice has two
entries. The station and platform choices are the route visits between the
neighbouring stops of that position. Add Stop opens after the last stop, or at the
last position that has choices when the destination is already in the timetable.
Insert After Selected needs a selected row and opens the dialog after it. When no
position has a station visit, Add Stop shows a message and opens no dialog. A
position whose neighbouring stops leave no station visit, or follow a stop that
does not resolve, shows the reason and blocks Accept. Accept inserts only when the
new stop resolves and no other stop changes its route visit; otherwise the dialog
stays open with the reason and the timetable is unchanged. Existing stops keep
their times, and the new stop gets the times entered in the dialog. Removing every
row gives a service with no scheduled calls.
Blank planned times are absent, while `0` is simulation zero. Use Elapsed for
seconds from zero or Clock for case-base time: with base `08:00:00`, `90` seconds
is `08:01:30`. Enter `+1d 00:00:00` for next midnight. Merely switching mode or
changing the case base time does not shift stored offsets. Entry stays elapsed
seconds. Invalid chronology blocks Run; incomplete departures and insufficient
dwell windows are warnings. These checks do not guarantee physical feasibility.

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
directory helper is `validateRunnableSceneDirectory`. `scene_tool validate
--runnable path/to/scene` prints the diagnostics of that run gating. Structural
loading is checked first so semantic reference diagnostics do not cascade from
malformed JSON.

Before committing a scene, validate it and run the native scene path. Use
`scene_tool export` only when a downstream legacy tool needs interoperability
files; edits to that export do not flow back into canonical JSON. The exporter
generates legacy infrastructure, signalling constraints, rolling stock,
timetables, and supported passenger CSVs from canonical data. It reports an
error when a canonical passenger window cannot be represented by the legacy
half-hour bucket format instead of silently changing it. The signalling areas of
the scene are exported to `TrackLines/AreasCaseStudy.txt`; an edit of that file
does not flow back.

## Scope

The V2 bundle changes packaging only. V1 JSON and `SceneModel` remain the
simulation-data contract.
