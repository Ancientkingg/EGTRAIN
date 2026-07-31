# Renderer style map

This map is the visual contract for the network playback primitives. State colors stay restrained so topology remains visible below operational overlays.

## Track operational state

`trackStatePriority()` orders overlays from base topology to the most restrictive state. A higher value wins when more than one state is available.

| State | Priority | Color | Pen | Meaning |
| --- | ---: | --- | --- | --- |
| Free | 0 | `#A0ACB4` | solid, 2 px | No operational reservation or occupation is reported. |
| Prepared | 1 | `#4C8DAE` | dash-dot, 6 px | A route is prepared for movement. |
| Occupied | 2 | `#D05A47` | solid, 8 px | A train or other movement occupies the section. |
| Blocked | 3 | `#D6A13A` | dashed, 8 px | The section is unavailable or restricted. |

`TrackLineItem`, `VirtualArcItem`, and `ConnectionItem` use the free-track style for base topology. `TrackLineItem` paints an operational underlay before that base stroke. Prepared, occupied, and blocked states differ by width and pen style as well as color.

Measured against the `#12191F` canvas, the contrast ratios are 7.64:1 for free, 4.84:1 for prepared, 4.42:1 for occupied, and 7.61:1 for blocked. Each exceeds the 3:1 target for graphical objects.

## Signal aspect

`classifySignalAspect()` maps the simulator aspect codes to lamp colors and a non-color cue:

| Code | Lamp | Cue |
| ---: | --- | --- |
| 0 | red (`#ff0000`) | stop bar |
| 75 | yellow (`#ffff00`) | caution triangle |
| 180 | green (`#00ff00`) | proceed chevron |
| 270 | green (`#00ff00`) | proceed chevron |
| other | neutral gray (`#808080`) | none |

`SignalItem` ignores view transformations. Below 3x, stop uses a prominent red circle with a bar, caution uses a prominent yellow triangle, and repeated proceed signals use a small muted-green tick. Signal posts and bases are hidden at this scale. At 3x and above, the full 12 by 16 housing, aspect icon, direction cue, post, and base return. `setReversedDirection(true)` points the direction cue left; `false` points it right.

## Train categories

Train badges use the following fill and outline pairs:

| Category | Fill | Outline | Shape |
| --- | --- | --- | --- |
| Passenger | `#9BA5AA` | `#4A5960` | rounded |
| Sprinter | `#86AA96` | `#3F6A54` | rounded |
| Intercity | `#C6A86E` | `#765623` | capsule |
| High speed | `#86A6B9` | `#3E627A` | rounded |
| Freight | `#A99787` | `#5D4C3F` | square |

Shape remains a second category cue. The badge keeps its identifier and optional speed text in one `QGraphicsItem` with `ItemIgnoresTransformations`. Long identifiers use middle elision so a distinguishing service suffix remains visible.

## Entity icons

Entity icons use 24 by 24 SVGs on a transparent canvas. Geometry stays inside the 2 to 22 coordinate range. Primary strokes use 1.5 px with round joins and caps, and rounded housings use a 3 px corner radius. The palette is `#101A22` for the surface, `#26313B` for housings, `#0D131A` for outlines, `#F2F5F7` for ink, `#5078D2` for stations, `#2AAA7A` for passengers, `#2ECC71` for Proceed, `#F2A516` for Caution, `#EF5350` for Stop, and `#7F8C95` for Neutral.

Stations render at 24 by 24 screen pixels. Stop stations use a white roundel, platform stations use two rail strokes and a platform stroke, and interchange stations use crossing rails with a transfer dot. Passenger icons render at 14 by 14 pixels and keep their passenger IDs in `PassengerItem` objects. Train category icons render at 14 by 14 pixels inside the badge: Passenger, Sprinter, Intercity, High speed, and Freight each have a distinct cab or locomotive shape. Signal icons occupy the 12 by 12 square at the top of the fixed 12 by 16 signal item. Neutral uses a gray lamp and center dot, Stop uses a red lamp and bar, Caution uses a yellow lamp and triangle, and Proceed uses a green lamp and chevron. The direction triangle remains in the lower signal area.

The application loads these assets through the Qt resource aliases under `:/icons/`. `MainWindow`, `TrainBadgeItem`, and `SignalItem` cache the pixmaps and draw them with smooth transformation. Station, passenger, train badge, and signal overlays ignore view transformations so their screen sizes remain stable while the network zooms.

## Application surfaces

The application shell is `#F2F3F1`, panels are `#FAFAF8`, primary text is `#24313A`, and dividers are `#C8CED2`. Primary actions and keyboard focus use `#315A70`. Warnings use `#965C22`.

`NetworkView` paints one dark neutral surface, `#12191F`, inside a `#3A4A54` border. It has no adaptive grid because the schematic has no displayed distance or coordinate unit. The map key lives in the Case and Layers panel and uses the same renderer classifications as the canvas.
