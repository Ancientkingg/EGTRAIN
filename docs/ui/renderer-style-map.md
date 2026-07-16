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

## Entity icons

Entity icons use 24 by 24 SVGs on a transparent canvas. Geometry stays inside the 2 to 22 coordinate range. Primary strokes use 1.5 px with round joins and caps, and rounded housings use a 3 px corner radius. The palette is `#101A22` for the surface, `#26313B` for housings, `#0D131A` for outlines, `#F2F5F7` for ink, `#5078D2` for stations, `#2AAA7A` for passengers, `#2ECC71` for Proceed, `#F2A516` for Caution, `#EF5350` for Stop, and `#7F8C95` for Neutral.

Stations render at 24 by 24 screen pixels. Stop stations use a white roundel, platform stations use two rail strokes and a platform stroke, and interchange stations use crossing rails with a transfer dot. Passenger icons render at 14 by 14 pixels and keep their passenger IDs in `PassengerItem` objects. Train category icons render at 14 by 14 pixels inside the badge: Passenger, Sprinter, Intercity, High speed, and Freight each have a distinct cab or locomotive shape. Signal icons occupy the 12 by 12 square at the top of the fixed 12 by 16 signal item. Neutral uses a gray lamp and center dot, Stop uses a red lamp and bar, Caution uses a yellow lamp and triangle, and Proceed uses a green lamp and chevron. The direction triangle remains in the lower signal area.

The application loads these assets through the Qt resource aliases under `:/icons/`. `MainWindow`, `TrainBadgeItem`, and `SignalItem` cache the pixmaps and draw them with smooth transformation. Station, passenger, train badge, and signal overlays ignore view transformations so their screen sizes remain stable while the network zooms.

## Surface, grid, and low zoom

`NetworkView` paints a dark neutral surface (`#101A22`) and a grid (`#1B2A35`) in `drawBackground()`. The grid uses no scene items. Its scene spacing doubles or halves around an 80-unit base so grid lines stay between 24 and 96 device pixels apart. At low zoom the grid therefore thins instead of filling the viewport, while badges and the legend remain screen-sized.

`NetworkLegendItem` uses the same track and train colors for prepared, occupied, blocked, Intercity, Sprinter, and Freight entries. It also ignores view transformations.
