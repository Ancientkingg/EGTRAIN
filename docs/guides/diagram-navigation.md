# Inspecting diagrams

This page describes the result windows: hover, select, zoom, pan and the data behind the charts. [How-to](how-to.md#results-and-exports) lists the result windows and the exports, [How-to](how-to.md#move-around-the-network-view-and-follow-a-train) covers the network view, and [Timetable graphs and train paths](#timetable-graphs-and-train-paths) describes what the two route charts plot.

Hover near a plotted line or event marker to inspect its nearest **recorded sample**. The tooltip shows that sample's values, units and available station, call or block context. A line between samples is useful for finding a train, but the tooltip does not interpolate a value at the pointer. Filled occupation regions can be inspected inside their boundaries when marked as such by the chart; ordinary closed outlines cannot.

Click a line or marker to select its train. The train filter controls visibility; neither filtering nor navigation changes the underlying CSV data schema. Drag a rectangle to zoom into an area. Two-finger scrolling pans by the supplied pixel distance; an ordinary wheel pans using wheel steps. Ctrl or Command plus wheel zooms around the pointer. Pinch zooms on devices that deliver native or Qt pinch gestures. Use + or - to zoom, arrow keys to pan while the chart has focus, and Home, 0 or Reset zoom to return to the chart's initial bounds. Reset does not remove a train selection or change filters.

A result window shows and exports the run it was opened for. A later run does not change an open window or its CSV export.

When the service of a train has a visualization colour, `DiagramWindow::setTrainColors` gives the series of that train the colour, keyed by the train id that groups the series. The line width, the dash pattern and the alpha of a filled region stay as the chart builder set them. Selecting and clearing a train keeps the colour, and the train filter shows it as the swatch of the train. A train without a colour keeps the colours the chart gave it, and its swatch is the pen colour of its first series. `DiagramWindow::setTrainListColors` changes the swatches only: the blocking-time charts use it, because the colours of their envelopes show the block type. `TimetableTableWindow::setTrainColors` shows the same swatch in the filter of the timetable table. The application builds the colours from the completed run and the scene that was run; the input traction characteristic has no trains and is not recoloured.

Timetable graphs plot planned and simulated station arrival/departure events and dwell; train-path graphs plot continuous simulated trajectory samples. Both use reference-route runtime X in kilometres horizontally and elapsed simulation time vertically, increasing downward from run start (0 s). Choose a reference from the routes used in the run; each choice lists the station ids the route passes in travel order (`route0 --> Gvc - Gdg - Ut`), and the tooltip holds the full text. The short graph title identifies the reference route and elapsed-zero origin. The horizontal axis shows the route-coordinate origin and travel direction. The station names stand above the plot, each with its position in kilometres where there is room. Only as many as fit side by side are named, so that no name is shortened, and zooming in names more of them. The chart shortens the title of an axis that is wider than the chart, for example one with a long route id; the tooltip over that title holds all of it. A wrapped note below the controls explains the runtime coordinate basis and omitted mapping. These are runtime route coordinates, not authored absolute chainage: scene nodes have X/Y and stations may have `position_km`, but the scene has no explicit chainage datum. The native infrastructure builder copies node X into runtime nodes, the route builder derives route sections from those nodes, and the operations builder retains the resolved stop node in each train. Shared canonical node or station IDs anchor other routes; mapping is limited to monotonic shared-anchor intervals. Unmapped or ambiguous portions are omitted rather than extrapolated. The graph CSV files (`timetable_graph.csv` and `train_path_graph.csv`) contain `Train`, `Reference route`, `Source route`, `Event`, `Station`, `Journey order`, `Call`, `Elapsed time[s]`, `Reference route X[km]`. Missing event times and unavailable projected positions are blank; CSV uses the displayed reference and currently visible trains. Existing timetable table and raw trajectory CSV schemas are unchanged.

Blocking-time charts draw each complete calculated blocking envelope as a translucent rectangle between the directed block endpoints and its displayed start/end times. A zero-length block is a thin boundary line. They do not measure occupation or reservation independently: the model combines approach minus setup and sight reaction through clearance plus release and run margin. The standard chart places recorded trajectory samples above the envelopes and planned station arrival/departure references in dashed lines; the compressed capacity chart shifts envelopes only and does not claim compressed train movement. All use reference-route runtime X horizontally and downward elapsed time vertically, including 0 s. For all routes, the first used route is the stated reference; segments and stops that cannot be projected from shared anchors, and incomplete blocks or missing clearance, are omitted rather than extrapolated. Selecting a route, train, block section or time window limits the displayed envelopes and recorded samples. The CSV keeps original and clipped envelope times, directed mapped endpoints and available components as separate columns; absent components stay blank. Trajectory rows identify actual recorded samples; planned event rows remain distinct. Filtering the visible trains also filters the export. Both blocking-time charts also draw neutral dotted physical boundaries and a narrow extent strip labelled `order:ID` over the native directed section endpoints. These are physical distance extents, not time envelopes. Labels keep their screen size, shorten long IDs and thin collisions as the chart zooms; hover the strip or a boundary for full IDs, route order and directed coordinates. Repeated/coincident sections remain separate occurrences in hover and export. The strip and any geometry notice occupy reserved chart space above the time plot, so they never hide early samples. They stay anchored while time pans, remain when every train is hidden, and appear in PNG export. Hovering a shortened or expanded visible label reports that label's full ID. Unusable or absent geometry has an in-chart notice instead of invented widths.

The CSV lists the displayed physical sections once after the train rows (Layer "block section extent", or "block section extent (geometry missing)"), even with no visible trains or envelopes. Section scopes retain route occurrence indices, not first-match IDs. Unavailable coordinates are blank. An envelope that cannot be projected onto the reference route is listed without X values (Layer "unmapped blocking envelope"). Trains with only unmapped rows still appear in the filter and can be exported without adding a plotted series. The last column, "Reference section", is the 1-based order of a unique matching reference section. Exact ID and interval matches take precedence over shared-resource overlaps; several candidates leave the order blank and the hover text reports ambiguity. The model never matches by interval alone. Capacity analysis refuses scopes whose selected resource also appears outside the selected occurrence range or whose entry resource has several candidate occurrences; unrelated repeated sections do not block a representable scope. It does not change the capacity calculation to guess an occurrence.

Time axes display clock labels without changing the simulation-second values. The diagram title also annotates elapsed zero when the clock starts at a nonzero time. A tooltip gets a break opportunity after every 32 characters of a run without a space, such as a long identifier, so that it wraps within the screen; the break opportunities are not drawn. A tooltip is dismissed when the pointer leaves the chart, its series is filtered, the window closes, or the chart is replaced.

## Timetable graphs and train paths

These two **Diagrams** and **Run Results > Open result view...** actions use the
same reference-route distance axis, but show different data:

- **Timetable graph** connects available planned
  and simulated station arrivals and departures in separate series and shows
  dwell at each stop as a vertical segment. Planned lines are dashed, simulated
  lines solid. Connections between stops are not continuous train movement.
- **Train paths** plots recorded position samples against
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
Both charts show physical reference sections separately as neutral boundaries and an
`order:ID` extent strip. Hover shows full IDs and directed runtime coordinates;
labels thin when crowded and remain with train filters and PNG export. Missing
geometry is reported inside the chart, never replaced by equal-width blocks.
Both CSVs list the displayed physical occurrences once, including a section-only
export when no train is visible. A unique Reference section order is blank when
matching is ambiguous. Envelopes that cannot be placed on the reference route
are exported without X values. Capacity analysis reports an unavailable scope
when its resource-ID calculation cannot distinguish the selected occurrences.

Input traction plots show rolling-stock input, not simulated effort. Their
warning remains visible when a curve contains negative effort below the default
`0 kN` lower bound; those values remain in the input, not removed or clamped by
the plot.
