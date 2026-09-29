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
| Segment/switch node | Scene-sized light-gray circular dot, without a device-pixel minimum. Transparent device-space hit geometry retains semantic picking without painting a minimum dot. |
| Station/platform node | Square, with the historical platform/interchange/ordinary colors and white platform bars. |
| Station name and pictogram | White name, scene-sized font (`station_size / 5`) centered at the authored station anchor. A white station-building SVG above it at scene scale, parented to its semantic station node for artwork hits. The map key and menus use the same shape in a dark variant. |
| Signal | Each authored direction has its own scene-sized aspect head (red Stop, yellow Caution, green Proceed), white cosmetic post and white base. No viewport slots, grouping, sectors, multiplicity labels or direction ticks. Transparent device-space hit geometry retains signal inspection at Fit. Selection does not overwrite aspects. |
| Train and passengers | Historical train polygons (passenger yellow, sprinter green, intercity yellow, high-speed blue, freight brown) and original scene-sized `pax_icon.png` passenger glyph, also used by the map key. Passenger information callouts remain separate operational overlays; no train category badge is drawn. |

Operational prepared, occupied and blocked states remain in scene data and
inspectors, but do not change ordinary historical track paint. Selection paints
the blue track stroke; unselected track keeps its speed-class color. The map key
shows only visible track styles. Layer/Follow/replay controls are unchanged.
