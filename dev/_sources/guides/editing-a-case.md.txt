# Editing and validating a case

This page describes the editors of a case and the rules for editing and validating it. For a first edit, follow [Tutorial: modify a bundled case](tutorial-modify-a-case.md).

## Infrastructure

The dock **Infrastructure** has a list above the table that chooses it: tracks, nodes, arcs, blocks, connections, stations, platforms, signals, signalling areas, routes and the other records. **Add** and **Delete** are under the table. Tracks, nodes and platforms have task buttons as well, described in [Tasks of tracks, nodes and platforms](#tasks-of-tracks-nodes-and-platforms). In the table of blocks, a track filter above the table chooses the track that **Add** appends to, and **Insert**, **Move Up** and **Move Down** follow **Delete**. While the table is empty, a line above it says what to add first, and **Add** stays off in Blocks until the case has a track. While a run uses the case, the table, **Add** and **Delete** are off, and the tooltip of **Delete** reads "Stop the run to edit." **Delete** is off until a row is selected. **Delete** deletes the row at once and names it in the status bar, such as `Deleted node 'node_3'. Undo restores it.`; **Edit > Undo** brings it back. A row that other rows refer to is refused with a message titled after the kind of row, such as Cannot Delete Track, which lists the first ten of them and counts the rest. The tables are built in dependency order in the [tutorial for the smallest useful case](tutorial-minimal-scene.md).

**Filter rows**, above the table, shows only the rows that hold every word you type, each in one of their cells, in any case. The line beside it counts the rows, such as "399 nodes", or "12 of 399 nodes" while a filter hides some. The filter stays when you choose another kind. Nodes, arcs, connections and platforms also have a track filter at the start of that line: it shows the nodes and the arcs on the track, the connections with an end on it and the platforms with a node on it. Blocks keep their own track filter. **Add** and **Insert** clear the filters, so that the new row shows, and **Move Up** and **Move Down** are off while a filter hides rows.

Nodes and platforms have a column **Station** with the name of the station that the node or platform belongs to, the station that **Details** names for a node; it is not edited there. The track of a node, the two nodes of an arc and the two nodes of a connection are chosen in a list that you can also type in. Typing shows the entries that contain the text, in any case. An arc lists the nodes of its track with their kilometres, and a connection lists every node with its track. An id that the case does not have is refused with a message that names it, such as "Node 'n9' does not exist", and an empty one with "Choose a node" or "Choose a track". The **Nodes** cell of a platform lists its node ids and is not typed in: **Choose Nodes...** or a double-click on the cell opens **Platform Nodes**, a list of the nodes of the case to check, with a filter field. A node of another platform or station shows that name and cannot be checked, and a node id that the case no longer has is listed as missing so that you can remove it. The flags (**Has position** of a station, **Has corridor** and **Reversed** of a route, **Has speed limit** of a connection and **Direction** of a station boundary) are check boxes: a click changes the flag, as one step for **Undo**.

While the dock **Infrastructure** is in view, the canvas marks the selected row in violet, under the tracks, and a line under the table says what it marks, such as "route2: 124 sections on B3, B0, B4, B5 and B2" or "Gourland.platform.1: node B0.node.41 at 2.520 km". A track is marked along its whole length, an arc or a connection along the line between its ends, and a block along its section. A node, a station mark, the platform nodes of a station, the nodes of a platform and the heads of a signal get a ring, and the bar of a platform an outline. A signalling area is marked over its kilometres, and a single-track restriction, a station boundary and a block dependency over the sections they name. A route is marked over all its sections and connections, with a disc at its start and an arrowhead at its end when its direction is known, and the section chosen under **Route sections** is drawn over it in light amber. Choosing another row moves only the mark: the canvas keeps its zoom and position, and centres on the new mark only when none of it is in view. An edit draws the canvas again. Closing the dock, or bringing another editor to the front of its tabs, hides the mark. It comes back for the same row when the dock is shown again, and the canvas keeps its zoom and position.

To find the row of something on the canvas, click it. A click on a node, a station, a track line, a connection or a signal head selects its row, and the list above the table switches to its kind. A track line selects its arc, or its track where no arc joins the two nodes. A station node selects its platform under Platforms. A signal head selects the block of its section under Blocks, and the track filter shows the track of that block. A filter that hides the row is cleared. The canvas keeps its zoom and position, and **Details** still shows the item. **Edit in Infrastructure** in the menu of the item does the same and brings the dock to the front. While a run or its results are on the canvas, a click only shows the item in **Details**.

### Tasks of tracks, nodes and platforms

These buttons are under the table of their kind. Each works on the selected row and makes one step of **Edit > Undo**. They are off while no row is selected or a run uses the case.

- **Reverse Track** turns the selected track around: it swaps the From node and the To node of every arc of the track, changes the sign of their gradients and reverses the order of its blocks. The nodes, their X and the ids and lengths of the blocks stay as they are. Use it when every arc of a track runs against its X, which Case Validation reports with `X goes down from ...`. When routes run over the track, the box **Track Reversed** lists them, the first ten and a count of the rest, for you to check. The status bar reads `Reversed track '<track>'. Undo restores it.` A track that is not one line of arcs is refused with `Track '<track>' is not one line of arcs. Case Validation names what to fix.`
- **Set X from Distance** reads the X and Y of the nodes of the selected track as map coordinates in km, sets X to the distance along the arcs from the first node, which gets 0, and sets Y to 0 ([bring in track geometry from GIS](how-to.md#bring-in-track-geometry-from-gis)). When blocks or station positions may no longer fit, the box **X Set from Distance** gives the new length of the track and the sum of its blocks, such as `Track 'B0' is now 12.400 km long; its blocks add up to 12.000 km.`, lists the blocks and the stations to check, and says that Case Validation lists the references that the new X values break. The status bar reads `Set X of <n> nodes of track '<track>' from the distance along its arcs. Undo restores them.` A node without a number in X or Y stops it (`Node '<node>' has no number in X or Y. Set it first.`), and a track whose X already is that distance is left as it is.
- **Add Station...** asks for the name of a new station at the selected node (`Name of the new station at node <node>:`) and adds the station with one platform, `<name>.platform.1`, at that node. The status bar reads `Added station '<name>' at node '<node>'. Undo removes it.` It refuses a node that already belongs to a station, an empty name, a name with a tab or a line break, a name that another station has, and a case that already has the most stations a run takes.
- **Choose Nodes...** opens **Platform Nodes** for the selected platform, as a double-click on its **Nodes** cell does. Nodes on the tracks of the platform come first. **OK** sets the checked nodes as the nodes of the platform.

## Services

The dock **Services** has a list of services above three tabs: **Service** for the fields of the selected service, **Timetable** for its stops and the display of planned times, and **Run occurrences** for the occurrences a run includes.

In Services, Category offers Intercity, Regional, High speed/international,
Freight, Metro/urban, Suburban and No category. Unknown imported values remain
visible until you choose a replacement. This label does not change train
performance, composition or stops. It survives duplication and case folder and case file
saves, but not legacy export.

Colour, directly below Category, sets the colour of the
service's trains. Choose... opens a colour dialog; Default removes the colour.
Without a colour, every train is drawn in the default yellow. The colour applies
to every occurrence of the service (201-1, 201-2, ...) on the canvas and in
replay, and the Map Key lists it with the service id. The colour applies from
the next run. Choosing it after a run discards the results of that run, so its
trains leave the canvas and the Map Key until the next run. In the result windows of
a run, the lines of those trains in the speed, time, distance and tractive
effort diagrams, the delay diagram, the timetable graph and the train-path
graph take the colour instead of a colour of the palette, with their line
widths and dash patterns unchanged, and the Trains list of the window shows it
as the line of each train; the timetable table shows it as a swatch in its train
filter. Trains without a colour take the colours of the palette. Where a window
has too many trains to give each train or service a colour, the trains are grey
and only the selected train takes its colour. The blocking-time charts give the
colour to the lines and the outlines of the blocking times of the train; critical
blocks and conflicts keep their own colours. A stored value that is not
a `#RRGGBB` colour is shown as "Invalid: " followed by the text and is kept until
you choose a colour or press Default; trains of that service use the default
yellow. Changing the colour counts as a case edit, so it discards the run
results and replay like any other service edit. It survives duplication and
case folder and case file saves, but not legacy export.

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
passenger draws and simulation results are excluded from case input. Likewise, legacy OL, TDS,
Rescheduling, and GUI files have their own active/inert/output classifications;
see the [migration matrix](v1-scene-migration-matrix.md) instead of inferring
behavior from a filename.

Named-scenario selection, entrance delays, incidents, and passengers execute
from canonical data. The runtime applies only the selected/default scenario.

## Validation

Run the structural and semantic validation command after edits. The path can
name a case folder or a case file:

```bash
./build/scene_tool validate path/to/case
```

The command preserves support for structurally valid, incomplete historical
cases. Run gating additionally uses `validateRunnableScene`; the equivalent
directory helper is `validateRunnableSceneDirectory`. `scene_tool validate
--runnable path/to/case` prints the diagnostics of that run gating. Structural
loading is checked first so semantic reference diagnostics do not cascade from
malformed JSON.

Before you share a case, validate it and run it on the native path. Use
`scene_tool export` only when a downstream legacy tool needs interoperability
files; edits to that export do not flow back into canonical JSON. The exporter
generates legacy infrastructure, signalling constraints, rolling stock,
timetables, and supported passenger CSVs from canonical data. It reports an
error when a canonical passenger window cannot be represented by the legacy
half-hour bucket format instead of silently changing it. The signalling areas of
the case are exported to `TrackLines/AreasCaseStudy.txt`, with their exact
values in `TrackLines/EGTRAINSignallingAreas.json`; an edit of those files does
not flow back. The station and track layout goes to `GUI/StationsCoord.txt` and
`GUI/caseStudyTrackData.txt`, from `views.json` when the case has one and
otherwise from the station positions. `GUI/unusedTracks.txt` lists the tracks
that `views.json` hides, and is written only when it hides one. Route
corridors, single-track restrictions and station boundaries go to
`GUI/caseStudyRouteCorridors.txt`, `GUI/singleTrackLimits.txt` and
`GUI/stationBoundarySections.txt` when the case has them. The base time and
the simulation settings go to `EGTRAINInitialParameters.json`. A case without
original legacy files prints the information `scene.export.info` that the legacy
files were generated from the case. The export warns (`scene.export.compatibility`)
about a station name with a tab or line break, about an empty corridor name or
one with a comma, tab or line break, and about the tracks that `views.json` draws
as double switches, because the text files cannot hold them.
