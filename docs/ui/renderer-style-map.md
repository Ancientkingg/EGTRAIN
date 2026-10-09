# Network renderer style map

The first imported renderer (`472d0b5`, `mainwindow.cpp`) is the railway-symbol
reference. Preview, playback and replay share current scene geometry, inspection
and layer controls. Canonical authored track/node geometry supersedes historical
case-specific drawing branches.

| Primitive | Presentation |
| --- | --- |
| Canvas | Background `#101a22` with a faint `#182832` grid in preview, live simulation and replay. Grid lines are cosmetic one-device-pixel lines on multiples of the spacing in scene coordinates, so they move with the scene while panning. Spacing starts at 80 scene units and is doubled or halved until it is 24 to 96 pixels on screen. |
| Speed-class track | At least 200 km/h: RGB (30,130,210), cosmetic width 4; at least 120: RGB (80,80,80), width 3; otherwise RGB (120,120,120), width 2. |
| Connection | White cosmetic width 2. |
| Segment/switch node | Shared preview/runtime scene-sized light-gray circular dot, without a device-pixel minimum. The current runnable-chain rule uses an ordinary first point and zero painted size for ordinary starts on four-node tracks; the final endpoint remains visible. Transparent device-space hit geometry retains semantic picking without painting a minimum dot. |
| Station/platform node | Shared preview/runtime square at later station endpoints, with the historical platform/interchange/ordinary colors. White platform bars follow passenger-GUI mode; preview has no passenger counters. Station and name layers do not hide structural squares. |
| Station name and pictogram | White name, font `station_size / 5` at scene scale, centered at the authored station anchor plus its derived decoration offset. A white station-building SVG above it, parented to its authored structural node where available for artwork hits. Both keep their anchor point (bottom centre of the pictogram, top centre of the name), offset and parent at every zoom. Their size on screen is the larger of the scene size and a minimum: 24 px high for the pictogram and a 12 px font for the name. Names that overlap on screen are hidden in order of selected, followed, platform count, then the existing tie breaks; the pictogram and its hit target stay. Preview artwork keeps its own canonical station identity; position-only artwork has no invented node. The map key and menus use the same shape in a dark variant. |
| Signal | Each authored direction has its own scene-sized aspect head, white cosmetic post and white base. No viewport slots, grouping, sectors, multiplicity labels or direction ticks. Transparent device-space hit geometry retains signal inspection at Fit. Selection does not overwrite aspects. A head has one of five states, with one text for the tooltip, the inspector and the end-to-end checks: Stop (red, codes 0 and 751), Caution (yellow, code 75), Proceed (green, codes 180 and 270), Unavailable (an empty gray ring, RGB (150,150,150)) and Failed (a red lamp with a white cross). A head is unavailable when its section has no signalling level, when no route runs through its section in its direction, and for a code that no aspect uses. A head is failed, on every head of the failed section that has a state, for the steps in which a signal failure incident is active. Levels 3 and 4 keep red and green. Heads in the preview before a run stay green. Marks add a second cue to the colour: a dark bar on Stop, a dark dot on Caution, a short gray dash in the Unavailable ring, the white cross on Failed; Proceed has none. A head draws its mark only when it is at least 6 device pixels wide; narrower heads show colour only. The map key has a row for each of the five states. |
| Train and passengers | Historical train polygons in one default yellow, (235,210,55) with outline (110,90,20), whatever the rolling-stock type. A train whose service has a valid `visualization_color` (Services dock, Service visualization colour) is drawn with that fill and its darker shade as the outline, for every occurrence on the canvas and in replay; an unknown service or an invalid text gives the default. The map key has a "Train" row in the default yellow when a train uses it, then one row per other fill colour labelled with the service ids that use it (the first three, then "and N more"; the tooltip lists all), ordered by the smallest service id of each row. Original scene-sized `pax_icon.png` passenger glyph, also used by the map key. Passenger information callouts remain separate operational overlays; no train category badge is drawn. |
| Selection | A ring in the cue colour (61,214,255) with a dark casing, drawn in screen pixels just below the item in the stacking order, so it keeps its size at every zoom. A line item gets a halo along its line, any other item a rounded ring of at least 16 by 16 pixels with 3 pixels of padding; a station node is ringed together with its pictogram. The ring never recolours or covers the item, is covered by the item and by everything stacked over it, and takes no mouse input. |

Contrast against the canvas `#101a22` (WCAG relative-luminance contrast ratio,
(L1 + 0.05) / (L2 + 0.05)). The grid is deliberately close to the canvas (1.16:1).

| Primitive | Colour | Contrast |
| --- | --- | --- |
| Track at least 200 km/h | (30,130,210) | 4.34 |
| Track at least 120 km/h | (80,80,80) | 2.18 |
| Other track | (120,120,120) | 3.99 |
| Connection, station name | White | 17.60 |
| Selection cue, track selected in the editor table | (61,214,255) | 10.27 |
| Signal Stop, Caution, Proceed | Red, yellow, green | 4.40, 16.39, 12.83 |
| Signal Unavailable ring | (150,150,150) | 5.95 |
| Train default | (235,210,55) | 11.57 |
| Track state prepared, occupied, blocked | `#4C8DAE`, `#D05A47`, `#D6A13A` | 4.80, 4.39, 7.56 |

Operational prepared, occupied and blocked states remain in scene data and
inspectors, but do not change ordinary historical track paint. Selection draws a
ring around the item and never recolours it, so a selected track keeps its
speed-class color. The cue colour is reserved: no built-in track, signal or default train colour uses
it; a service colour chosen by the user is not checked. The map key shows only visible track styles. Layer/Follow/replay controls are
unchanged.

Preview uses canonical read-only fields and marks runtime-only information
unavailable before Run. Signal head identity is the rendered canonical track,
section and direction, plus any authored signal IDs, not a numeric runtime
placeholder. Runtime numeric inspection remains separate. Expanded hit shapes
do not determine preview topology bounds or Fit. These implementation and
semantic checks do not establish matched-scale historical visual acceptance.

Scene-space presentation distances use the fixed measured historical
construction convention `Q_h = 800000 * 100 / 360`, converted by TrackPreview's
actual normalization `S_c / Q_h`. This includes artwork/text geometry and
annotation nudges, node/signal dimensions, platform/passenger adornments, train
lateral thickness and callouts. It does not rescale physical interpolation or
cosmetic strokes. Fonts retain their original metrics through fractional item
scaling. Only wholly successful visible authored station-view projections enable
conversion; raw, mixed, invalid, empty, degenerate or nonfinite scenes keep factor
1. See [Historical track presentation](historical-track-comparison.md) for the
reference convention and remaining visual acceptance limits.
