# #109 Revamp The In-Scene Entity Icons (Stations, Passengers, Trains, Signals)

- GitHub: https://github.com/Ancientkingg/EGTRAIN/issues/109
- State: open
- Labels: `type: feature`, `area: ui`
- Created: 2026-07-06
- Mirrored: 2026-07-06

## Summary

Design and ship a cohesive icon set for the in-scene entities — stations, passengers, trains, and signals — so the network view reads as one deliberate visual system instead of a couple of stock PNGs plus ad-hoc painted shapes.

## Current state

- **Stations** use `train_station.png` (a single raster, 6.3 KB), drawn via a `QPixmap` (`app/MainWindow.cpp:217`, rendered through `graphics/items/IconItem`).
- **Passengers** use `pax_icon.png` (15.2 KB), drawn through `graphics/items/PassengerItem` (`app/MainWindow.h:299` `pax_pixmap` / `pax_pixmap_scaled`).
- **Trains** are custom-painted polygons/bodies (`graphics/items/TrainItemGroup`, `TrainBodyItem`), not icons.
- **Signals** are custom-painted (`graphics/items/SignalItem`).
- The result is visually inconsistent: two different raster styles for stations/passengers and hand-drawn geometry for trains/signals, with no shared line weight, palette, or grid.

## Scope

- Define a small **icon design spec**: shared grid size, stroke weight, corner radius, and a restrained palette that works on the network background (light and dark), consistent with the UI refresh (#79) and the rendering redesign (#80).
- Produce vector-first source art (SVG) for: **station** (and station variants if types differ), **passenger**, **train** (per category where categories exist), and **signal aspects**. Rasterize to the resolutions the scene needs, or render SVG directly where practical.
- Ensure icons stay legible at the zoom levels the playback view uses and degrade gracefully when small (tie into the readability goals in #80).
- Wire the new artwork through the `resources/` folder and `:/` resource paths introduced in the resources-consolidation issue (#108); retire `train_station.png` / `pax_icon.png`.
- Update `IconItem` / `PassengerItem` and the train/signal painters to consume the new assets/spec.

## Acceptance criteria

- Stations, passengers, trains, and signals share one visual language (grid, weight, palette).
- Icons are legible at default zoom and at the dense-station zoom used in the visual smokes.
- New artwork is delivered as versioned source (SVG) plus the rendered assets, loaded from Qt resources — no loose PNGs.
- A before/after screenshot of the default case is attached to the issue.

## Notes

Depends on the resources-folder consolidation (#108, where the art lives) and coordinates with #79 (UI shell refresh) and #80 (train/track rendering) so entity icons, tracks, and chrome share one look. Application/desktop icon is handled separately.
