# Historical track presentation

The visual reference is the initial imported renderer `472d0b5`, particularly
`paintNode`, `paintStationNode`, `paintStationIcon`, `paintStationName`,
`arcDrawing`, `paintConnection`, `paintSignal` and `paintTrain`. The later
`f138134` manifest repair does not change these symbols. The renderer style map
records the primitive-by-primitive restoration.

The earlier Copenhagen comparison images remain useful as records of the
pre-restoration state, not as acceptance captures for this change:

![Historical Copenhagen at Fit](images/copenhagen-historical-fit.png)
![Previous Copenhagen restoration at Fit](images/copenhagen-restored-fit.png)

Canonical authored geometry and Fit padding remain in use, so comparisons must distinguish geometry from symbol presentation. Station artwork uses a station-building SVG while retaining the historical scene-space anchor and size. The historical passenger PNG is restored as a separate resource; train polygon colors use the original `VisualPolish.cpp` palette. The original renderer did not paint a train category badge or speed label over the locomotive.
