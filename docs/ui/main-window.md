# Main window

## Playback

The speed slider in the toolbar sets how fast a run plays; right is faster. The filled part of the track, `#315a70`, shows how far the slider is towards the fastest setting; the rest of the track is `#7b8b96`. The handle is a solid `#315a70` circle with a white border. It turns `#26495c` under the pointer and while it is dragged, gets a `#24313a` border when it has keyboard focus, and the handle and the filled part turn `#9aa5ab` when the slider is disabled. The replay slider in the replay row has the same look.

The readout next to the slider shows the current speed as **Fastest** or as a factor of real time such as **4.0x** or **100x**. Its width fits the longest text, so the toolbar does not move while the slider is dragged. The slider has no Slower and Faster words beside it: with them the toolbar needs about 1070 px and no longer shows every control in a 1024 px window.

## Run results header

The dialog **Run simulation** shows the duration of the run as H:MM:SS, the format of the **Travel time** column of Run Results, followed by the clock time at which the run ends, as in `2:30:00 (ends near 09:40:40)`.

The text at the top of Run Results puts each entry on a line of its own: `Run: <case> / <scenario>`, `Occurrences: ...` and `Status: Completed`. With advanced details on, the direct incident evidence and `Destination terminations: <count>` follow, and `Delay baseline: <run>` comes last when a baseline is set. A long entry wraps inside its own line.

When Run Results first opens at the bottom of the window it takes a third of the window height, so the network and the replay row keep the rest. A height that the user drags is kept while the dock stays open; a new run that opens the closed dock sets a third again.

The Case and Layers dock opens at the width of its widest layer toggle, **Stations and platforms**, plus the vertical scroll bar. A case name up to that width fits on one line, and a longer one wraps. The user can drag the dock wider.
