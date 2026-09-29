# Network renderer style map

The track symbols follow the early renderer in `f138134` (unchanged from
`472d0b5`). The reference captures are in `local/reports/issue-360/`.
The current canonical preview and runtime retain their corrected geometry.

| Primitive | Historical appearance | Current exception |
| --- | --- | --- |
| Canvas | Solid black, no grid | None. Application chrome is unchanged. |
| Speed-class track | At least 200 km/h: RGB (30,130,210), 4 px; at least 120: RGB (80,80,80), 3 px; otherwise RGB (120,120,120), 2 px. Cosmetic strokes. | Canonical preview uses its authored arc speed; unmatched segments use local gray. Selected track retains the existing highlight. |
| Connection | White, 2 px | None. |
| Boundary/switch node | Light-gray circular dot | A separate 3-device-pixel child owns Fit dot painting, bounds and hit geometry. Historical Copenhagen double switches used a zero-size dot; their supplied topology coordinate is retained instead. |
| Station node | Square; platform dark gray, interchange blue, other stop gray | None. |
| Station name and pictogram | White spaced name and the original `train_station.png` above the track | One fixed-size picture/label per station and existing collision placement keep names readable at Fit instead of copying historical subpixel symbols and overlapping labels. |
| Signal | Circular aspect plate (Stop red, Caution yellow, Proceed green), white mast and base at detail | Plates retain a device-visible minimum at Fit and a small direction tick. Each signal cue occupies a local device-space slot, including singletons. Slots are sized for the largest plate and direction ticks; a partial viewport-edge strip joins its adjacent full slot so neighboring cues cannot overlap. Nearby anchors share one compact cue rather than scattering across the viewport: equal-size colored sectors retain each distinct aspect (including Stop), ticks indicate the represented directions, and a center digit or `+` indicates multiplicity. The tooltip and a scrollable list in the existing signal inspector list every constituent section/signal identity, direction and aspect; preview uses actual canonical signal IDs when provided and otherwise names the derived boundary section. No in-view anchor is dropped at viewport edges. Groups recompute after aspect, zoom, pan and layer changes. A selected constituent remains the group's representative through merge/split and keeps its live inspector identity; scene teardown clears this observer before deletion. Selection outlines do not replace the operational aspect fill. |

## Operational underlays

The operational overlay remains independent of speed-class base topology. A
free track has no underlay. Permissive signalling uses `#4C8DAE` dash-dot 5 px,
occupied uses `#D05A47` solid 6 px, and blocked uses `#D6A13A` dashed 5 px.
The base track is painted over its underlay. The map key remains outside the
canvas and shows speed classes in both modes, adding operational states in playback.
Layer visibility, selection and Follow remain independent of the palette.
