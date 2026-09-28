# Historical track presentation

## Reference

The reference is `f138134` (Repair Netherlands train manifest). Its rendering code is unchanged from the initial import, `472d0b5`. The manifest repair lets the Netherlands case load trains; it does not change the symbols. This version was built and run on macOS 26.6.2 without source changes.

The reference implementations are `mainwindow.cpp` (`paintNode`, `paintStationNode`, `paintStationIcon`, `paintStationName`, `arcDrawing`, `paintConnection`, `paintSignal`) and `VisualPolish.cpp` at that revision.

## Copenhagen at Fit

Both captures use a 1280×696 application window with side and bottom panels hidden, at Fit. Each image is an unscaled 1256×500 crop centered vertically within the network viewport. The different toolbar heights are outside the crops.

Historical:

![Historical Copenhagen at Fit](images/copenhagen-historical-fit.png)

Restored:

![Restored Copenhagen at Fit](images/copenhagen-restored-fit.png)

These are visual comparisons, not pixel-identical scene renders. Current canonical geometry and Fit padding remain in use. The historical rendering makes many signals, station symbols and labels nearly invisible at this scale. The restored rendering retains the original assets and palette, with the visibility exceptions below.

## Restored styling and necessary exceptions

| Element | Historical styling retained | Exception |
| --- | --- | --- |
| Canvas | Solid black | None |
| Tracks | RGB 30/130/210, width 4 at 200+ km/h; RGB 80/80/80, width 3 at 120+ km/h; RGB 120/120/120, width 2 below that | Independent operational and selection underlays remain |
| Connections | White cosmetic 2px stroke | None |
| Boundary dots | Light-gray circles | A declared 3px minimum remains visible at Fit; zero-size switch dots retain their actual coordinates |
| Station nodes | Original square and platform/interchange colors | Current canonical station identities remain |
| Station pictogram | Original `train_station.png` | Screen-sized placement and label collision handling keep names usable |
| Signal plates | Circular aspect fills and white mast/base strokes | Device-visible minimum size; compact local combined cues preserve dense aspects and constituent identities |
| Selection | Separate from operational state | A signal's aspect fill is never replaced by the selection color |

Combined signals use separated local slots rather than long connector lines. Distinct aspects remain visible as sectors; direction ticks and the member list identify the represented signals. The runtime inspector follows the selected member when groups merge or split. See [the renderer style map](renderer-style-map.md) for dimensions and interaction details.
