# #80 Redesign Train And Track Rendering For Readable Network Playback

- GitHub: https://github.com/Ancientkingg/EGTRAIN/issues/80
- State: open
- Labels: `type: feature`, `area: ui`, `priority: p1`
- Created: 2026-07-04
- Mirrored: 2026-07-04

## Summary

Redesign the network playback scene so users can read train identity, route state, track occupation, signal aspects, station context, and delay state at normal zoom.

The issue references:

- `EGTRAIN/QEGTRAIN/myQGraphicsView.cpp`
- `EGTRAIN/QEGTRAIN/mainwindow.cpp`
- `EGTRAIN/QEGTRAIN/VisualPolish.cpp`
- `EGTRAIN/QEGTRAIN/trainQGraphicsPolygonItem.cpp`
- `docs/ui/2026-07-04-ui-refresh-proposal.md`
- `docs/ui/egtrain-ui-refresh-mock.svg`
- `docs/ui/egtrain-ui-refresh-mock.png`

## Design Constraints

- Visual quality should come from railway state, not decoration.
- Use custom `QGraphicsItem` painting for tracks, trains, signals, stations, labels, speed badges, and legend elements.
- Use `QGraphicsView::setBackgroundBrush()` or `drawBackground()` for the map surface and grid.
- Keep scene item counts and repaint regions controlled.
- Use restrained state colors for free track, prepared route, occupied section, blocked section, signal aspect, train category, and delay.

## Acceptance Criteria

- Default case screenshot shows hierarchy between background, tracks, routes, occupied sections, trains, stations, and signals.
- At least three train categories or states have distinct colors or glyphs.
- Signal aspects are visible at default zoom and remain clickable.
- Occupied and blocked sections are visible without hiding base topology.
- Train labels and speed or delay badges avoid overlap where practical and degrade gracefully at low zoom.
- `VisualPolish.cpp` is expanded or replaced by a documented renderer style map.
- Visual smoke coverage includes default view, dense station zoom, and follow-train state.

## Notes

The GitHub issue comment embeds the rendered PNG mockup and links the SVG source from the `_gh-attach-assets` release.

`npx impeccable detect docs/ui` returned `ok` on 2026-07-04.
