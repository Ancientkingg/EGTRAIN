# Editing and validating a case

This page describes the editors of a case and the rules for editing and validating it. For a first edit, follow [Tutorial: modify a supplied case](tutorial-modify-a-case.md).

## Infrastructure

The dock **Infrastructure** has a list above the table that chooses it: tracks, nodes, arcs, blocks, connections, stations, platforms, signals, signalling areas, routes and the other records. **Add** and **Delete** are under the table. In the table of blocks, a track filter above the table chooses the track that **Add** appends to, and **Insert**, **Move Up** and **Move Down** follow **Delete**. While the table is empty, a line above it says what to add first, and **Add** stays off in Blocks until the scene has a track. While a run uses the case, the table, **Add** and **Delete** are off, and the tooltip of **Delete** reads "Stop the run to edit." **Delete** is off until a row is selected. **Delete** asks for confirmation. A row that other rows refer to is refused with a message titled after the kind of row, such as Cannot Delete Track, which lists the first ten of them and counts the rest. The tables are built in dependency order in the [tutorial for the smallest useful scene](tutorial-minimal-scene.md).

## Services

The dock **Services** has a list of services above three tabs: **Service** for the fields of the selected service, **Timetable** for its stops and the display of planned times, and **Run occurrences** for the occurrences a run includes.

In Services, Category offers Intercity, Regional, High speed/international,
Freight, Metro/urban, Suburban and No category. Unknown imported values remain
visible until you choose a replacement. This label does not change train
performance, composition or stops. It survives duplication and folder/bundle
saves, but not legacy export.

Colour, directly below Category, sets the colour of the
service's trains. Choose... opens a colour dialog; Default removes the colour.
Without a colour, every train is drawn in the default yellow. The colour applies
to every occurrence of the service (201-1, 201-2, ...) on the canvas and in
replay, and the Map Key lists it with the service id. The colour applies from
the next run: the Map Key keeps the colour of the last run until then, also
after Default. In the result windows of
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

## Timetable stops

Click a timetable row to edit its station, platform, dwell, arrival and departure.
OK applies the fields together; Cancel leaves the row unchanged. Add Stop
uses the same dialog with an Insert position chooser: "At the start of the
timetable" or "After stop k" for each stop, so a station that occurs twice has two
entries. The station and platform choices are the route visits between the
neighbouring stops of that position. Add Stop opens after the last stop, or at the
last position that has choices when the destination is already in the timetable.
Insert After Selected needs a selected row and opens the dialog after it. When no
position has a station visit, Add Stop shows a message and opens no dialog. A
position whose neighbouring stops leave no station visit, or follow a stop that
does not resolve, shows the reason and blocks OK. OK inserts only when the
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

## Records, scenarios and passengers

Keep IDs unique and keep service links consistent; [Links between records](scene-schema.md#links-between-records) lists the links.

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

## Validation

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
the scene are exported to `TrackLines/AreasCaseStudy.txt`, with their exact
values in `TrackLines/EGTRAINSignallingAreas.json`; an edit of those files does
not flow back. The station and track layout goes to `GUI/StationsCoord.txt` and
`GUI/caseStudyTrackData.txt`, from `views.json` when the scene has one and
otherwise from the station positions. `GUI/unusedTracks.txt` lists the tracks
that `views.json` hides, and is written only when it hides one. Route
corridors, single-track restrictions and station boundaries go to
`GUI/caseStudyRouteCorridors.txt`, `GUI/singleTrackLimits.txt` and
`GUI/stationBoundarySections.txt` when the scene has them. The base time and
the simulation settings go to `EGTRAINInitialParameters.json`. A case without
original legacy files prints the information `scene.export.info` that the legacy
files were generated from the case. The export warns (`scene.export.compatibility`)
about a station name with a tab or line break, about an empty corridor name or
one with a comma, tab or line break, and about the tracks that `views.json` draws
as double switches, because the text files cannot hold them.
