# Main window

## Playback

The speed slider in the toolbar sets how fast a run plays; right is faster. The filled part of the track, `#315a70`, shows how far the slider is towards the fastest setting; the rest of the track is `#7b8b96`. The handle is a solid `#315a70` circle with a white border. It turns `#26495c` under the pointer and while it is dragged, gets a `#24313a` border when it has keyboard focus, and the handle and the filled part turn `#9aa5ab` when the slider is disabled. The replay slider in the replay row has the same look.

The readout next to the slider shows the current speed as **Fastest** or as a factor of real time such as **4.0x** or **100x**. Its width fits the longest text, so the toolbar does not move while the slider is dragged. The slider has no Slower and Faster words beside it: with them the toolbar needs about 1070 px and no longer shows every control in a 1024 px window.

## Toolbar buttons and Follow

Every toolbar button has a white fill and a `#7b8b96` outline. A disabled button, such as Pause and Stop before a run, has a grey `#e4e8eb` fill, a light `#c9d3db` outline and grey text. Under the pointer and with keyboard focus a button turns `#e4eaee` with a `#315a70` outline. **Open Case** keeps its own darker fill and lighter outline.

**Follow** shows a crosshair icon beside its text. While Follow is on, the button has a light `#d3e3ec` fill, a `#315a70` outline and dark text; under the pointer or with focus the fill turns `#bcd2df` and the outline `#24313a`.

The status bar shows the zoom of the network view as **Zoom: Fit** for the whole network and as a factor such as **Zoom: 2x** otherwise.

## Run results header

The dialog **Run simulation** shows the duration of the run as H:MM:SS, the format of the **Travel time** column of Run Results, followed by the clock time at which the run ends, as in `2:30:00 (ends near 09:40:40)`.

The text at the top of Run Results puts each entry on a line of its own: `Run: <case> / <scenario>`, `Occurrences: ...` and `Status: Completed`. With advanced details on, the direct incident evidence and `Destination terminations: <count>` follow, and `Delay baseline: <run>` comes last when a baseline is set. A long entry wraps inside its own line.

When Run Results first opens at the bottom of the window it takes a third of the window height, or the height that its content needs when that is more, so the network and the replay row keep the rest. A height that the user drags is kept while the dock stays open; a new run that opens the closed dock sets a third again.

The Case and Layers dock opens at the width of its widest layer toggle, **Stations and platforms**, plus the vertical scroll bar. A case name up to that width fits on one line, and a longer one wraps. The user can drag the dock wider.

## Tables and progress bar

Every table and list has a light header: `#e3eaee` sections with `#24313a` semibold text, padding of 4 by 6 px and a `#c9d3db` line on the right and below each section. The corner button of a table has the same face. Alternate rows are `#f4f7f9` between the `#eef2f5` rows; the colour is set in the stylesheet, so a dark system theme does not darken them under the dark text. Column widths and header texts are unchanged.

The timetable table colours a delay above 60 s (160,30,30), a delay above 0 s (125,85,0) and any other delay (0,100,0). Each has a contrast of at least 5.8:1 on both row colours.

The progress bar of a run fills in `#315a70`. Its text is `#24313a` on the empty part, 11.4:1 on `#eceeec`, and white on the fill, 7.4:1: `TimeProgressBar` draws the bar a second time over the fill with the stylesheet's `selection-color` as the text colour.

In Run Results the **Open result view...** button has the white face, grey border and padding of the **Export CSV...** and **Export PNG...** buttons beside it, the same `#315a70` border under the pointer or with focus, and the same grey face when disabled.
