# Inspecting diagrams

Hover near a plotted line or event marker to inspect its nearest **recorded sample**. The tooltip shows that sample's values, units and available station, call or block context. A line between samples is useful for finding a train, but the tooltip does not interpolate a value at the pointer. Filled occupation regions can be inspected inside their boundaries when marked as such by the chart; ordinary closed outlines cannot.

Click a line or marker to select its train. The train filter controls visibility; neither filtering nor navigation changes the underlying CSV data schema. Drag a rectangle to zoom into an area. Two-finger scrolling pans by the supplied pixel distance; an ordinary wheel pans using wheel steps. Ctrl or Command plus wheel zooms around the pointer. Pinch zooms on devices that deliver native or Qt pinch gestures. Use + or - to zoom, arrow keys to pan while the chart has focus, and Home, 0 or Reset zoom to return to the chart's initial bounds. Reset does not remove a train selection or change filters.

Time axes display clock labels without changing the simulation-second values. A tooltip is dismissed when the pointer leaves the chart, its series is filtered, the window closes, or the chart is replaced.
