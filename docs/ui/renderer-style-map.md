# Network renderer style map

The first imported renderer (`472d0b5`, `mainwindow.cpp`) is the railway-symbol
reference. Preview, playback and replay share current scene geometry, inspection
and layer controls. Canonical authored track/node geometry supersedes historical
case-specific drawing branches.

| Primitive | Presentation |
| --- | --- |
| Canvas | Solid black. |
| Speed-class track | At least 200 km/h: RGB (30,130,210), cosmetic width 4; at least 120: RGB (80,80,80), width 3; otherwise RGB (120,120,120), width 2. |
| Connection | White cosmetic width 2. |
| Segment/switch node | Shared preview/runtime scene-sized light-gray circular dot, without a device-pixel minimum. The current runnable-chain rule uses an ordinary first point and zero painted size for ordinary starts on four-node tracks; the final endpoint remains visible. Transparent device-space hit geometry retains semantic picking without painting a minimum dot. |
| Station/platform node | Shared preview/runtime square at later station endpoints, with the historical platform/interchange/ordinary colors. White platform bars follow passenger-GUI mode; preview has no passenger counters. Station and name layers do not hide structural squares. |
| Station name and pictogram | White name, scene-sized font (`station_size / 5`) centered at the authored station anchor. A white station-building SVG above it at scene scale, parented to its authored structural node where available for artwork hits. Preview artwork keeps its own canonical station identity; position-only artwork has no invented node. The map key and menus use the same shape in a dark variant. |
| Signal | Each authored direction has its own scene-sized aspect head (red Stop, yellow Caution, green Proceed), white cosmetic post and white base. No viewport slots, grouping, sectors, multiplicity labels or direction ticks. Transparent device-space hit geometry retains signal inspection at Fit. Selection does not overwrite aspects. |
| Train and passengers | Historical train polygons (passenger yellow, sprinter green, intercity yellow, high-speed blue, freight brown) and original scene-sized `pax_icon.png` passenger glyph, also used by the map key. Passenger information callouts remain separate operational overlays; no train category badge is drawn. |

Operational prepared, occupied and blocked states remain in scene data and
inspectors, but do not change ordinary historical track paint. Selection paints
the blue track stroke; unselected track keeps its speed-class color. The map key
shows only visible track styles. Layer/Follow/replay controls are unchanged.

Preview uses canonical read-only fields and marks runtime-only information
unavailable before Run. Signal head identity is the rendered canonical track,
section and direction, plus any authored signal IDs, not a numeric runtime
placeholder. Runtime numeric inspection remains separate. Expanded hit shapes
do not determine preview topology bounds or Fit. These implementation and
semantic checks do not establish matched-scale historical visual acceptance.
