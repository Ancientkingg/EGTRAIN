# Renderer style map

This map is the visual contract for the network playback primitives. State colors stay restrained so topology remains visible below the overlay.

## Track operational state

`trackStatePriority()` orders overlays from base topology to the most restrictive state. A higher value wins when more than one state is available.

| State | Priority | Color | Pen | Meaning |
| --- | ---: | --- | --- | --- |
| Free | 0 | none (`#00000000`) | no underlay | No operational reservation or occupation is reported. |
| Prepared | 1 | `#2ECC71` | solid, 8 px | A route is prepared for movement. |
| Occupied | 2 | `#EF5350` | solid, 10 px | A train or other movement occupies the section. |
| Blocked | 3 | `#F2A516` | dashed, 8 px | The section is unavailable or restricted. The dash pattern remains visible in monochrome output. |

`TrackLineItem` paints the operational underlay first, then its normal track pen. Free sections skip the underlay, so the existing topology pen remains the only stroke.

## Signal aspect

`classifySignalAspect()` maps the simulator aspect codes to lamp colors and a non-color cue:

| Code | Lamp | Cue |
| ---: | --- | --- |
| 0 | red (`#ff0000`) | stop bar |
| 75 | yellow (`#ffff00`) | caution triangle |
| 180 | green (`#00ff00`) | proceed chevron |
| 270 | green (`#00ff00`) | proceed chevron |
| other | neutral gray (`#808080`) | none |

`SignalItem` draws a fixed 12 by 16 housing, a lamp cue, and a direction cue while ignoring view transformations. `setReversedDirection(true)` points the direction cue left; `false` points it right. Existing callers that still set the public `reversedDirection` field are supported.

## Train categories

Train badges reuse the existing `TrainVisual` palette and a shape classification. Intercity is a capsule, Sprinter is rounded, and Freight is square, so the categories remain distinct without relying on color alone. The badge keeps its identifier and optional speed text in one `QGraphicsItem` with `ItemIgnoresTransformations`, so labels stay readable while the map zooms.

## Surface, grid, and low zoom

`NetworkView` paints a dark neutral surface (`#101A22`) and a grid (`#1B2A35`) in `drawBackground()`. The grid uses no scene items. Its scene spacing doubles or halves around an 80-unit base so grid lines stay between 24 and 96 device pixels apart. At low zoom the grid therefore thins instead of filling the viewport, while badges and the legend remain screen-sized.

`NetworkLegendItem` uses the same track and train colors for prepared, occupied, blocked, Intercity, Sprinter, and Freight entries. It also ignores view transformations.
