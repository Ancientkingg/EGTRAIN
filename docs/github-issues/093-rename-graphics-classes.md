# #93 Rename The Qt Graphics Classes To Describe What They Draw

- GitHub: https://github.com/Ancientkingg/EGTRAIN/issues/93
- State: open
- Labels: `refactor`, `area: ui`
- Created: 2026-07-05
- Mirrored: 2026-07-05

Part of the source-tree reorganization epic (#91).

## Summary

The Qt graphics classes are named after the base class they extend, not the thing they render. `myQGraphicsEllipseItem` draws a network node. `myQGraphicsRectItem` draws a station node (its handler is `MainWindow::displayStationNodeInfo`). `signallingQGraphicsEllipseItem` draws a signal. The names force a reader to open the file to learn what it is.

This issue renames the file and the class together and updates every use site, cast, forward declaration, and include guard.

## Renames

| Current | New |
| --- | --- |
| `myQGraphicsScene` | `NetworkScene` |
| `myQGraphicsView` | `NetworkView` |
| `myQGraphicsItem` | `BaseNetworkItem` |
| `myQGraphicsEllipseItem` | `NodeItem` |
| `myQGraphicsRectItem` | `StationNodeItem` |
| `myQGraphicsLineItem` | `TrackLineItem` |
| `myQGraphicsPixmapItem` | `IconItem` |
| `myQGraphicsColorizeEffect` | `HighlightEffect` |
| `connectionQGraphicsLineItem` | `ConnectionItem` |
| `passengerQGraphicsPixmapItem` | `PassengerItem` |
| `platformQGraphicsRectItem` (+ `platformQGraphicsItem.cpp`) | `PlatformItem` |
| `signallingQGraphicsEllipseItem` | `SignalItem` |
| `trainQGraphicsItemGroup` | `TrainItemGroup` |
| `trainQGraphicsPolygonItem` | `TrainBodyItem` |
| `virtualArcQGraphicsLineItem` | `VirtualArcItem` |
| `timeQProgressBar` | `TimeProgressBar` |
| `infoQDockWidget` | `InfoDockWidget` |

## Notes

- `platformQGraphicsItem.cpp` and `platformQGraphicsRectItem.h` are a mismatched pair today: the `.cpp` and `.h` basenames differ. `PlatformItem.cpp` and `PlatformItem.h` fix that.
- `myQGraphicsItem` has no instantiations in the current tree; keep it as `BaseNetworkItem` only if something still derives from it, otherwise fold it into #95 for removal.

## Acceptance criteria

- No class or file name begins with `my` or ends in a bare Qt item type (`QGraphicsEllipseItem`, `QGraphicsLineItem`, and so on).
- `qgraphicsitem_cast<...>` sites use the new type names.
- `cmake --build` succeeds; `ctest` reports 12/12.

