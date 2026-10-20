# Main window

## Playback

The Pause button reads **Resume** while a run is paused. It is as wide as the longer text, so the toolbar does not move, and its outline shows the whole button, so the gaps from Run to Pause and from Pause to Stop are equal.

The replay row starts with the icon buttons **Start**, **Play** and **End**, each with a tooltip and an accessible name; Tab goes from Start to Play and End and then to the slider. While the replay plays, **Play** shows the pause icon, the tooltip and the name **Pause**. The icons have one size, so End and the slider do not move. The label after the slider shows the clock time of the shown frame in the format of the progress bar and is as wide as its longest text, so the slider keeps its width while the user seeks.

The speed slider in the toolbar sets how fast a run plays; right is faster. The filled part of the track, `#315a70`, shows how far the slider is towards the fastest setting; the rest of the track is `#7b8b96`. The handle is a solid `#315a70` circle with a white border. It turns `#26495c` under the pointer and while it is dragged, gets a `#24313a` border when it has keyboard focus, and the handle and the filled part turn `#9aa5ab` when the slider is disabled. The replay slider in the replay row has the same look.

The readout next to the slider shows the current speed as **Fastest** or as a factor of real time such as **4.0x** or **100x**. Its width fits the longest text, so the toolbar does not move while the slider is dragged. The slider has no Slower and Faster words beside it: with them the toolbar needs about 1070 px and no longer shows every control in a 1024 px window.

## Toolbar buttons and Follow

Every toolbar button has a white fill and a `#7b8b96` outline. A disabled button, such as Pause and Stop before a run, has a grey `#eceeec` fill, a light `#c9d3db` outline and grey text. Under the pointer and with keyboard focus a button turns `#e3eaee` with a `#315a70` outline. **Open Case** keeps its own darker fill and lighter outline.

The two group separators of the toolbar are 1 px wide lines as tall as the toolbar row less the margins of the style sheet, at every window width. A menu separator is a 1 px `#c9d3db` line, the colour of the menu border, at every scale.

A tooltip names the shortcut of its button as the platform writes it, such as Ctrl+R, or ⌘R on macOS.

**Follow** shows a crosshair icon beside its text. While Follow is on, the button has a light `#d3e3ec` fill, a `#315a70` outline and dark text; under the pointer or with focus the fill turns `#bcd2df` and the outline `#24313a`.

The closed train list ends a name that does not fit its 180 px with an ellipsis; the open list and the tooltip show the whole text.

The status bar shows the zoom of the network view as **Zoom: Fit** for the whole network and as a factor such as **Zoom: 2x** otherwise. The Follow label beside it shows one clause, such as **Rail-1 enters at 08:25:50** or **Following Rail-1**, at most 400 px wide and elided with an ellipsis beyond that; its tooltip gives the whole explanation. Follow messages on the left of the status bar are the same clause, and a run that ends says **Simulation complete - results in the Diagrams menu**. A message wider than the room left of the fields on the right ends in an ellipsis.

## Run results header

The dialog **Run simulation** shows the duration of the run as H:MM:SS, the format of the **Travel time** column of Run Results, followed by the clock time at which the run ends, as in `2:30:00 (ends near 09:40:40)`.

The text at the top of Run Results puts each entry on a line of its own: `Run: <case> / <scenario>`, `Occurrences: ...` and `Status: Completed`. With advanced details on, the direct incident evidence and `Destination terminations: <count>` follow, and `Delay baseline: <run>` comes last when a baseline is set. A long entry wraps inside its own line.

When Run Results first opens at the bottom of the window it takes a third of the window height, or the height that its content needs with the table header and three rows when that is more, but at most about half of the height that it shares with the network view, so the network and the replay row keep the rest. A dock lower than its content scrolls, and the table shows at least its header and one row. The delay comparison follows the table, so that in a maximised window on a 1024 by 768 screen the table header and a row show without scrolling. A height that the user drags is kept while the dock stays open; a new run that opens the closed dock sets that height again.

The Case and Layers dock opens at the width of its widest layer toggle, **Stations and platforms**, plus the vertical scroll bar. A case name up to that width fits on one line, and a longer one wraps. The user can drag the dock wider.

The map key has a **Passenger count** row only while the network shows passengers: the run started with passengers on, the case has passengers and the Passengers layer is on.

The editor docks share one group of tabs on the right of the window, so each open editor has the full height beside the network, also with Run Results open below it. Choosing an editor in the Editors menu opens it, or brings it to the front when it is open behind another tab. An editor dock, such as Rolling stock units, opens at least as wide as its size hint, also after another dock has been dragged narrower, so its fields do not scroll sideways. An editor dock floated with its button or a double click opens inside the screen, as large as its content asks where the screen allows. The tabs of the group keep their whole names and scroll when they do not fit.

The Services dock shows its list above its tabs, so that beside Case and Layers in a window of 819 px it needs no horizontal scroll bar. Tabs that do not fit scroll and keep their whole names.

The lists of Rolling stock units, Services, Incidents and Entrance delays show about three rows, with their three buttons in one row below, so that the fields of the selected item keep the height of a short dock. The title of the dock names the list of Rolling stock units and the list of Services. The list of Services keeps its height on the Timetable tab.

## Light look

EGTRAIN has one light look, also when the system uses a dark appearance. On macOS the application keeps the light Aqua appearance, so native controls, menus, dialogs and title bars stay light. On other systems a dark system palette is replaced at startup by the light standard palette of the widget style, so selected rows, tooltips and charts stay light; Qt 6.8 and later is also asked for the light colour scheme. The title bar belongs to the window manager outside macOS and can still follow a dark system setting.

## Focus and buttons

A focused push button shows a `#315a70` border and no rectangle around its text. The filled default button of a dialog shows focus as a `#315a70` ring separated from its fill by a light line. In **Run simulation** only **Run simulation** is filled: **Cancel** keeps the plain face when it has the focus, although Enter then presses it.

A button that opens a menu, such as **Open result view...** in Run Results and **Trains** in a result window, keeps 20 px on its right for the arrow. A drop-down list, such as the train list beside **Follow**, draws its arrow without a frame of its own.

## Tables and progress bar

Tables follow [Interface style](interface-style.md#tables). Their colours are set in the stylesheet, so a dark system theme does not darken them under the dark text.

In the Run Results table the train and the operating code are left-aligned; the numbers and times are right-aligned.

The timetable table colours a delay above 60 s `#81352d`, a delay above 0 s `#74451c` and any other delay `#006400`. Each has a contrast of at least 6.9:1 on both row colours.

The progress bar of a run fills in `#315a70`. Its text is `#24313a` on the empty part, 11.4:1 on `#eceeec`, and white on the fill, 7.4:1: `TimeProgressBar` draws the bar a second time over the fill with the stylesheet's `selection-color` as the text colour.

In Run Results the **Open result view...** button has the white face, text size, grey border and padding of the **Export CSV...** and **Export PNG...** buttons beside it, the same `#315a70` border under the pointer or with focus, and the same grey face when disabled.

## Keyboard

Every menu of the menu bar has an Alt mnemonic: **F**ile, **E**dit, **V**iew, Ed**i**tors, **S**imulation, **H**elp and **D**iagrams. macOS does not show them.

In Run Results, Tab reaches **Open result view...**, **Export CSV...** and **Export PNG...** before the table, and leaves the table for the delay baseline buttons after it; the arrow keys move between the cells. The table has a `#c9d3db` border that turns `#315a70` while the table has the focus. The tab bars of tabbed docks and the scroll areas of the docks are not in the Tab order: Tab reaches the controls inside a dock, and the menus raise a dock. The network view takes the arrow keys and the zoom shortcuts, and its 2 px `#3a4a54` border turns `#5f8fa8` while it has the focus.

Some platform behaviour is left as it is. Space activates a focused toolbar button and Enter does not, the platform convention for buttons. The native file dialog on Windows follows the dark mode of Windows while the window stays light. In Qt 5 the check box indicators do not grow with `QT_SCALE_FACTOR`, and Windows display scaling enlarges the text but not the icons, since the application does not set `AA_EnableHighDpiScaling`; Qt 6 scales both (#955, #956).
